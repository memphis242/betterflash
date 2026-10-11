#include "SearchWorker.h"
#include <QCryptographicHash>
#include <QDataStream>
#include <QDir>
#include <QFileInfo>
#include <QSqlQuery>
#include <QThread>
#include <QUuid>
#include <algorithm>
#include <cmath>

namespace {
QVariantList hitMaps(const QList<struct betterflash::search::SearchHit> &hits) {
    QVariantList result;
    for (const struct betterflash::search::SearchHit &hit : hits)
        result.append(QVariantMap{{QStringLiteral("id"), hit.id}, {QStringLiteral("tier"), hit.tier},
                                  {QStringLiteral("score"), hit.score}});
    return result;
}
QString hash(const QString &text) {
    return QString::fromLatin1(QCryptographicHash::hash(text.toUtf8(), QCryptographicHash::Sha256).toHex());
}
QStringList chunks(const QString &text) {
    QStringList result;
    // Overlap keeps a phrase that crosses a chunk boundary searchable.
    for (qsizetype start = 0; start < text.size();) {
        qsizetype end = std::min(start + 384, text.size());
        if (end < text.size()) {
            const qsizetype space = text.lastIndexOf(QLatin1Char(' '), end);
            if (space > start + 280) end = space;
        }
        result.append(text.mid(start, end - start));
        if (end == text.size()) break;
        start = end - 48;
    }
    if (result.isEmpty()) result.append(QStringLiteral("Empty flashcard"));
    return result;
}
}

SearchWorker::SearchWorker(QString cachePath, std::shared_ptr<std::atomic<quint64>> epoch)
    : m_cachePath(std::move(cachePath)), m_currentEpoch(std::move(epoch)) {}
SearchWorker::~SearchWorker() {
    const QString name = m_cache.connectionName();
    if (m_cache.isValid()) m_cache.close();
    m_cache = QSqlDatabase();
    if (!name.isEmpty()) QSqlDatabase::removeDatabase(name);
}
bool SearchWorker::current(quint64 epoch) const { return m_currentEpoch->load() == epoch; }

void SearchWorker::search(quint64 epoch, const QVariantList &cards, const QVariantList &decks,
                          const QString &deck, const QString &query) {
    Q_ASSERT(QThread::currentThread() == thread());
    if (!current(epoch)) return;
    m_epoch = epoch;
    m_query = betterflash::search::normalizedQuery(query);
    m_documents = betterflash::search::documents(cards, decks, deck);
    m_jobs.clear(); m_inFlight.clear(); m_vectors.clear(); m_documentKeys.clear();
    if (!current(epoch)) return;
    const auto literal = betterflash::search::literalMatches(m_documents, m_query);
    m_textMatchIds.clear();
    for (const struct betterflash::search::SearchHit &hit : literal) m_textMatchIds.insert(hit.id);
    emit literalReady(epoch, hitMaps(literal));
    QList<struct betterflash::search::SearchHit> fuzzy;
    for (const struct betterflash::search::SearchDocument &doc : m_documents) {
        if (!current(epoch)) return;
        fuzzy.append(betterflash::search::fuzzyMatches({doc}, m_query));
    }
    std::stable_sort(fuzzy.begin(), fuzzy.end(), [](const auto &a, const auto &b) {
        return a.score != b.score ? a.score > b.score : a.id < b.id;
    });
    for (const struct betterflash::search::SearchHit &hit : fuzzy) m_textMatchIds.insert(hit.id);
    if (current(epoch)) emit fuzzyReady(epoch, hitMaps(fuzzy));
}

bool SearchWorker::openCache() {
    if (m_cache.isOpen()) return true;
    if (!QDir().mkpath(QFileInfo(m_cachePath).absolutePath())) return false;
    if (!m_cache.isValid()) m_cache = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"),
        QStringLiteral("search-") + QUuid::createUuid().toString(QUuid::WithoutBraces));
    m_cache.setDatabaseName(m_cachePath);
    m_cache.setConnectOptions(QStringLiteral("QSQLITE_BUSY_TIMEOUT=1500"));
    if (!m_cache.open()) return false;
    QSqlQuery query(m_cache);
    if (!query.exec(QStringLiteral("PRAGMA journal_mode=WAL"))
        || !query.exec(QStringLiteral("CREATE TABLE IF NOT EXISTS embeddings (scope TEXT NOT NULL, key TEXT NOT NULL, vector BLOB NOT NULL, PRIMARY KEY(scope,key))"))) {
        m_cache.close();
        return false;
    }
    return pruneCache();
}
bool SearchWorker::pruneCache() {
    QSqlQuery size(m_cache);
    if (!size.exec(QStringLiteral("SELECT COUNT(*),COALESCE(SUM(length(vector)),0) FROM embeddings")) || !size.next()) return false;
    while (size.value(0).toLongLong() > 8192 || size.value(1).toLongLong() > 64 * 1024 * 1024) {
        size.finish();
        QSqlQuery trim(m_cache);
        if (!trim.exec(QStringLiteral("DELETE FROM embeddings WHERE rowid IN (SELECT rowid FROM embeddings ORDER BY rowid LIMIT 512)"))) return false;
        if (!size.exec(QStringLiteral("SELECT COUNT(*),COALESCE(SUM(length(vector)),0) FROM embeddings")) || !size.next()) return false;
    }
    return true;
}
void SearchWorker::clearCache(quint64 epoch) {
    if (!current(epoch)) return;
    if (!openCache()) { emit failed(epoch, QStringLiteral("SEARCH_CACHE_OPEN: Cannot rebuild the search cache. Check storage access.")); return; }
    QSqlQuery query(m_cache);
    if (!query.exec(QStringLiteral("DELETE FROM embeddings")) || !query.exec(QStringLiteral("VACUUM"))) {
        emit failed(epoch, QStringLiteral("SEARCH_CACHE_CLEAR: Cannot clear the search cache. Close other BetterFlash windows and retry.")); return;
    }
    m_vectors.clear(); m_jobs.clear(); m_inFlight.clear();
}
QVector<float> SearchWorker::cached(const QString &key) {
    QSqlQuery query(m_cache);
    query.prepare(QStringLiteral("SELECT vector FROM embeddings WHERE scope=? AND key=?"));
    query.addBindValue(m_scope); query.addBindValue(key);
    if (!query.exec()) { m_cacheError = QStringLiteral("SEARCH_CACHE_READ: Cannot read the embedding cache. Check storage access and retry."); return {}; }
    if (!query.next()) return {};
    const QByteArray bytes = query.value(0).toByteArray();
    if (bytes.size() < 8 || bytes.size() > 65540) return {};
    QDataStream input(bytes);
    input.setByteOrder(QDataStream::LittleEndian);
    input.setFloatingPointPrecision(QDataStream::SinglePrecision);
    quint32 size = 0; input >> size;
    if (size == 0 || size > 16384 || bytes.size() != 4 + 4 * size) return {};
    QVector<float> result(size);
    double norm = 0;
    for (float &value : result) { input >> value; if (!std::isfinite(value)) return {}; norm += double(value) * value; }
    if (input.status() != QDataStream::Ok || std::abs(norm - 1) > 0.01) return {};
    return result;
}
bool SearchWorker::save(const QString &key, const QVector<float> &vector) {
    QByteArray bytes;
    QDataStream output(&bytes, QIODevice::WriteOnly);
    output.setByteOrder(QDataStream::LittleEndian);
    output.setFloatingPointPrecision(QDataStream::SinglePrecision);
    output << quint32(vector.size());
    for (const float value : vector) output << value;
    QSqlQuery query(m_cache);
    query.prepare(QStringLiteral("INSERT OR REPLACE INTO embeddings(scope,key,vector) VALUES(?,?,?)"));
    query.addBindValue(m_scope); query.addBindValue(key); query.addBindValue(bytes);
    if (!query.exec()) return false;
    return ++m_cacheWrites % 128 != 0 || pruneCache();
}

void SearchWorker::semantic(quint64 epoch, const QString &profile, const QString &directory,
                            const QString &scope, bool remote) {
    if (!current(epoch) || m_epoch != epoch || m_documents.isEmpty()) {
        if (current(epoch)) emit semanticReady(epoch, {});
        return;
    }
    m_scope = scope;
    m_jobs.clear(); m_inFlight.clear(); m_vectors.clear(); m_documentKeys.clear(); m_cacheError.clear();
    if (!openCache()) { emit failed(epoch, QStringLiteral("SEARCH_CACHE_OPEN: Cannot open the embedding cache. Check free space and storage permissions, then retry.")); return; }
    const auto enqueue = [this](const QString &text, bool query) {
        const QString key = hash((query ? QStringLiteral("query-v1:") : QStringLiteral("document-v1:")) + text);
        if (!m_vectors.contains(key)) {
            const auto vector = cached(key);
            m_vectors.insert(key, vector);
            if (vector.isEmpty()) m_jobs.append(Job{key, text, query});
        }
        return key;
    };
    m_queryKey = enqueue(m_query, true);
    for (const struct betterflash::search::SearchDocument &doc : m_documents) {
        if (!current(epoch)) return;
        QStringList keys;
        for (const QString &text : chunks(doc.embeddingText)) keys.append(enqueue(text, false));
        m_documentKeys.insert(doc.id, keys);
    }
    if (!m_cacheError.isEmpty()) { emit failed(epoch, m_cacheError); return; }
    m_total = m_jobs.size(); m_completed = 0;
    emit progress(epoch, 0, m_total);
    if (remote) { nextRemote(); return; }
    if (!m_jobs.isEmpty()) {
        if (!m_embedder || m_loadedProfile != profile || m_loadedDirectory != directory) {
            m_embedder = std::make_unique<LocalEmbedder>();
            const auto loaded = m_embedder->load(directory, profile);
            if (!loaded) { m_embedder.reset(); emit failed(epoch, loaded.error()); return; }
            m_loadedProfile = profile; m_loadedDirectory = directory;
        }
        for (const struct Job &job : std::as_const(m_jobs)) {
            if (!current(epoch)) return;
            const auto encoded = m_embedder->encode(job.text, job.query);
            if (!encoded) { emit failed(epoch, encoded.error()); return; }
            if (!save(job.key, *encoded)) { emit failed(epoch, QStringLiteral("SEARCH_CACHE_WRITE: Cannot save embeddings. Check free space and retry.")); return; }
            m_vectors.insert(job.key, *encoded);
            emit progress(epoch, ++m_completed, m_total);
        }
    }
    if (current(epoch)) finish();
}

void SearchWorker::nextRemote() {
    if (!current(m_epoch)) return;
    if (m_jobs.isEmpty()) { finish(); return; }
    m_inFlight.clear();
    QStringList texts;
    while (!m_jobs.isEmpty() && m_inFlight.size() < 8) {
        const struct Job job = m_jobs.takeFirst();
        m_inFlight.append(job); texts.append(job.text);
    }
    emit remoteNeeded(m_epoch, texts);
}
void SearchWorker::acceptRemote(quint64 epoch, const QVariantList &vectors) {
    if (!current(epoch) || m_epoch != epoch) return;
    if (vectors.size() != m_inFlight.size()) { emit failed(epoch, QStringLiteral("SEARCH_RESPONSE: The provider returned an unexpected number of embeddings. Check model compatibility.")); return; }
    int dimensions = m_vectors.value(m_queryKey).size();
    for (qsizetype i = 0; i < vectors.size(); ++i) {
        const QVariantList values = vectors[i].toList();
        if (values.isEmpty() || values.size() > 16384 || (dimensions && values.size() != dimensions)) {
            emit failed(epoch, QStringLiteral("SEARCH_DIMENSIONS: Embedding dimensions do not match. Use one compatible embedding model and retry.")); return;
        }
        dimensions = values.size();
        QVector<float> vector; vector.reserve(dimensions);
        double norm = 0;
        for (const QVariant &value : values) {
            bool ok = false; const double number = value.toDouble(&ok);
            if (!ok || !std::isfinite(number) || std::abs(number) > 1e10) { emit failed(epoch, QStringLiteral("SEARCH_VECTOR: Provider returned an invalid embedding.")); return; }
            vector.append(float(number)); norm += number * number;
        }
        if (norm <= 1e-20) { emit failed(epoch, QStringLiteral("SEARCH_VECTOR: Provider returned an empty embedding.")); return; }
        const double scale = std::sqrt(norm);
        for (float &value : vector) value = float(value / scale);
        const QString key = m_inFlight[i].key;
        if (!save(key, vector)) { emit failed(epoch, QStringLiteral("SEARCH_CACHE_WRITE: Cannot save embeddings. Check free space and retry.")); return; }
        m_vectors.insert(key, vector);
    }
    m_completed += vectors.size();
    emit progress(epoch, m_completed, m_total);
    nextRemote();
}
void SearchWorker::finish() {
    const auto query = m_vectors.value(m_queryKey);
    if (query.isEmpty()) { emit semanticReady(m_epoch, {}); return; }
    QList<struct betterflash::search::SearchHit> hits;
    for (const struct betterflash::search::SearchDocument &doc : m_documents) {
        if (!current(m_epoch)) return;
        if (m_textMatchIds.contains(doc.id)) continue;
        double best = -1;
        for (const QString &key : m_documentKeys.value(doc.id)) {
            const auto vector = m_vectors.value(key);
            if (vector.size() != query.size()) { emit failed(m_epoch, QStringLiteral("SEARCH_DIMENSIONS: Cached embeddings have incompatible dimensions. Change the embedding model identifier or rebuild its cache.")); return; }
            double score = 0;
            for (qsizetype i = 0; i < query.size(); ++i) score += double(query[i]) * vector[i];
            best = std::max(best, score);
        }
        // Similarity is a ranking score, not a calibrated probability of relevance.
        if (best >= 0.35) hits.append(betterflash::search::SearchHit{doc.id, 4, float(best)});
    }
    std::stable_sort(hits.begin(), hits.end(), [](const auto &a, const auto &b) {
        return a.score != b.score ? a.score > b.score : a.id < b.id;
    });
    if (hits.size() > 12) hits.resize(12);
    emit semanticReady(m_epoch, hitMaps(hits));
}
