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

SearchWorker::SearchWorker(QString cachePath, Epochs epochs,
                           std::shared_ptr<std::atomic<quint64>> testEpoch)
    : m_cachePath(std::move(cachePath)), m_currentEpochs(std::move(epochs)),
      m_testEpoch(std::move(testEpoch)) {
    for (const auto &epoch : m_currentEpochs) Q_ASSERT(epoch);
    Q_ASSERT(m_testEpoch);
}

void SearchWorker::testLocal(quint64 epoch, const QString &profile, const QString &directory) {
    Q_ASSERT(QThread::currentThread() == thread());
    if (m_testEpoch->load() != epoch) return;
    const auto fail = [this, epoch](const QString &message) {
        if (m_testEpoch->load() == epoch) emit testFinished(epoch, false, message);
    };
    auto embedder = std::make_unique<LocalEmbedder>();
    const auto loaded = embedder->load(directory, profile);
    if (!loaded) { fail(loaded.error()); return; }
    static const QStringList samples{
        QStringLiteral("Multiplication combines equal groups of quantities."),
        QStringLiteral("Multiplying numbers finds the total in repeated groups."),
        QStringLiteral("A recipe uses flour, water, yeast, and a warm oven.")};
    QVector<QVector<float>> vectors;
    vectors.reserve(samples.size());
    for (const QString &sample : samples) {
        if (m_testEpoch->load() != epoch) return;
        const auto encoded = embedder->encode(sample, false);
        if (!encoded) { fail(encoded.error()); return; }
        if (encoded->isEmpty()) { fail(QStringLiteral("SEARCH_TEST: The model returned an empty embedding.")); return; }
        vectors.append(*encoded);
    }
    const int dimensions = vectors.front().size();
    if (dimensions <= 0 || vectors[1].size() != dimensions || vectors[2].size() != dimensions) {
        fail(QStringLiteral("SEARCH_TEST: The model returned inconsistent vector dimensions.")); return;
    }
    const auto cosine = [dimensions](const QVector<float> &left, const QVector<float> &right) {
        double result = 0.0;
        for (int index = 0; index < dimensions; ++index) result += double(left.at(index)) * right.at(index);
        return result;
    };
    const double self = cosine(vectors.front(), vectors.front());
    const double related = cosine(vectors.front(), vectors.at(1));
    const double unrelated = cosine(vectors.front(), vectors.at(2));
    if (!std::isfinite(self) || !std::isfinite(related) || !std::isfinite(unrelated)
        || std::abs(self - 1.0) > 0.02) {
        fail(QStringLiteral("SEARCH_TEST: The model returned invalid or unnormalized vectors.")); return;
    }
    if (m_testEpoch->load() == epoch)
        emit testFinished(epoch, true, QStringLiteral("Model responded with %1-dimensional normalized vectors. Related-text similarity: %2; unrelated-text similarity: %3.")
            .arg(dimensions).arg(related, 0, 'f', 3).arg(unrelated, 0, 'f', 3));
}
SearchWorker::~SearchWorker() {
    const QString name = m_cache.connectionName();
    if (m_cache.isValid()) m_cache.close();
    m_cache = QSqlDatabase();
    if (!name.isEmpty()) QSqlDatabase::removeDatabase(name);
}
struct SearchWorker::QueryState &SearchWorker::state(int channel) {
    Q_ASSERT(channel >= 0 && channel < channelCount);
    Q_ASSERT(QThread::currentThread() == thread());
    return m_states[channel];
}
bool SearchWorker::current(int channel, quint64 epoch) const {
    Q_ASSERT(channel >= 0 && channel < channelCount);
    return m_currentEpochs[channel]->load() == epoch;
}

void SearchWorker::search(int channel, quint64 epoch, const QVariantList &cards, const QVariantList &decks,
                          const QString &deck, const QString &query) {
    if (!current(channel, epoch)) return;
    struct QueryState &work = state(channel);
    work = QueryState{};
    work.epoch = epoch;
    work.query = betterflash::search::normalizedQuery(query);
    work.documents = betterflash::search::documents(cards, decks, deck);
    if (!current(channel, epoch)) return;
    const auto literal = betterflash::search::literalMatches(work.documents, work.query);
    for (const struct betterflash::search::SearchHit &hit : literal) work.textMatchIds.insert(hit.id);
    emit literalReady(channel, epoch, hitMaps(literal));
    QList<struct betterflash::search::SearchHit> fuzzy;
    for (const struct betterflash::search::SearchDocument &doc : work.documents) {
        if (!current(channel, epoch)) return;
        fuzzy.append(betterflash::search::fuzzyMatches({doc}, work.query));
    }
    std::stable_sort(fuzzy.begin(), fuzzy.end(), [](const auto &a, const auto &b) {
        return a.score != b.score ? a.score > b.score : a.id < b.id;
    });
    for (const struct betterflash::search::SearchHit &hit : fuzzy) work.textMatchIds.insert(hit.id);
    if (current(channel, epoch)) emit fuzzyReady(channel, epoch, hitMaps(fuzzy));
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
void SearchWorker::clearCache() {
    Q_ASSERT(QThread::currentThread() == thread());
    m_memoryCache.clear();
    for (struct QueryState &work : m_states) work = QueryState{};
    QString error;
    if (!openCache()) error = QStringLiteral("SEARCH_CACHE_OPEN: Cannot rebuild the search cache. Check storage access.");
    else {
        QSqlQuery query(m_cache);
        if (!query.exec(QStringLiteral("DELETE FROM embeddings")) || !query.exec(QStringLiteral("VACUUM")))
            error = QStringLiteral("SEARCH_CACHE_CLEAR: Cannot clear the search cache. Close other BetterFlash windows and retry.");
    }
    if (!error.isEmpty()) {
        for (int channel = 0; channel < channelCount; ++channel)
            emit failed(channel, m_currentEpochs[channel]->load(), error);
    }
}
QVector<float> SearchWorker::cached(const QString &scope, const QString &key, QString &error) {
    const QString memoryKey = scope + QLatin1Char('\n') + key;
    if (const auto *vector = m_memoryCache.object(memoryKey)) return *vector;
    QSqlQuery query(m_cache);
    query.prepare(QStringLiteral("SELECT vector FROM embeddings WHERE scope=? AND key=?"));
    query.addBindValue(scope); query.addBindValue(key);
    if (!query.exec()) { error = QStringLiteral("SEARCH_CACHE_READ: Cannot read the embedding cache. Check storage access and retry."); return {}; }
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
    m_memoryCache.insert(memoryKey, new QVector<float>(result), int(result.size() * sizeof(float)));
    return result;
}
bool SearchWorker::save(const QString &scope, const QString &key, const QVector<float> &vector) {
    QByteArray bytes;
    QDataStream output(&bytes, QIODevice::WriteOnly);
    output.setByteOrder(QDataStream::LittleEndian);
    output.setFloatingPointPrecision(QDataStream::SinglePrecision);
    output << quint32(vector.size());
    for (const float value : vector) output << value;
    QSqlQuery query(m_cache);
    query.prepare(QStringLiteral("INSERT OR REPLACE INTO embeddings(scope,key,vector) VALUES(?,?,?)"));
    query.addBindValue(scope); query.addBindValue(key); query.addBindValue(bytes);
    if (!query.exec()) return false;
    m_memoryCache.insert(scope + QLatin1Char('\n') + key, new QVector<float>(vector), int(vector.size() * sizeof(float)));
    return ++m_cacheWrites % 128 != 0 || pruneCache();
}

void SearchWorker::semantic(int channel, quint64 epoch, const QString &profile, const QString &directory,
                            const QString &scope, bool remote) {
    if (!current(channel, epoch)) return;
    struct QueryState &work = state(channel);
    if (work.epoch != epoch || work.documents.isEmpty()) {
        emit semanticReady(channel, epoch, {});
        return;
    }
    work.scope = scope; work.profile = profile; work.directory = directory;
    work.jobs.clear(); work.inFlight.clear(); work.vectors.clear(); work.documentKeys.clear();
    if (!openCache()) { emit failed(channel, epoch, QStringLiteral("SEARCH_CACHE_OPEN: Cannot open the embedding cache. Check free space and storage permissions, then retry.")); return; }
    QString cacheError;
    const auto enqueue = [this, &work, &cacheError](const QString &text, bool query) {
        const QString key = hash((query ? QStringLiteral("query-v1:") : QStringLiteral("document-v1:")) + text);
        if (!work.vectors.contains(key)) {
            const auto vector = cached(work.scope, key, cacheError);
            work.vectors.insert(key, vector);
            if (vector.isEmpty()) work.jobs.append(Job{key, text, query});
        }
        return key;
    };
    work.queryKey = enqueue(work.query, true);
    for (const struct betterflash::search::SearchDocument &doc : work.documents) {
        if (!current(channel, epoch)) return;
        QStringList keys;
        for (const QString &text : chunks(doc.embeddingText)) keys.append(enqueue(text, false));
        work.documentKeys.insert(doc.id, keys);
    }
    if (!cacheError.isEmpty()) { emit failed(channel, epoch, cacheError); return; }
    work.total = work.jobs.size(); work.completed = 0;
    emit progress(channel, epoch, 0, work.total);
    if (remote) nextRemote(channel, epoch);
    else localStep(channel, epoch);
}

void SearchWorker::localStep(int channel, quint64 epoch) {
    if (!current(channel, epoch)) return;
    struct QueryState &work = state(channel);
    if (work.epoch != epoch) return;
    if (work.jobs.isEmpty()) { finish(channel, epoch); return; }
    const struct Job job = work.jobs.takeFirst();
    QString cacheError;
    auto vector = cached(work.scope, job.key, cacheError);
    if (!cacheError.isEmpty()) { emit failed(channel, epoch, cacheError); return; }
    if (vector.isEmpty()) {
        if (!m_embedder || m_loadedProfile != work.profile || m_loadedDirectory != work.directory) {
            m_embedder = std::make_unique<LocalEmbedder>();
            const auto loaded = m_embedder->load(work.directory, work.profile);
            if (!loaded) { m_embedder.reset(); emit failed(channel, epoch, loaded.error()); return; }
            m_loadedProfile = work.profile; m_loadedDirectory = work.directory;
        }
        if (!current(channel, epoch)) return;
        const auto encoded = m_embedder->encode(job.text, job.query);
        if (!encoded) { emit failed(channel, epoch, encoded.error()); return; }
        vector = *encoded;
        if (!save(work.scope, job.key, vector)) { emit failed(channel, epoch, QStringLiteral("SEARCH_CACHE_WRITE: Cannot save embeddings. Check free space and retry.")); return; }
    }
    work.vectors.insert(job.key, vector);
    emit progress(channel, epoch, ++work.completed, work.total);
    // One passage per event-loop turn keeps both search channels responsive.
    QMetaObject::invokeMethod(this, [this, channel, epoch] { localStep(channel, epoch); }, Qt::QueuedConnection);
}

void SearchWorker::nextRemote(int channel, quint64 epoch) {
    if (!current(channel, epoch)) return;
    struct QueryState &work = state(channel);
    if (work.epoch != epoch) return;
    work.inFlight.clear();
    QStringList texts;
    QString cacheError;
    while (!work.jobs.isEmpty() && work.inFlight.size() < 8) {
        const struct Job job = work.jobs.takeFirst();
        const auto vector = cached(work.scope, job.key, cacheError);
        if (!cacheError.isEmpty()) { emit failed(channel, epoch, cacheError); return; }
        if (!vector.isEmpty()) { work.vectors.insert(job.key, vector); ++work.completed; continue; }
        work.inFlight.append(job); texts.append(job.text);
    }
    emit progress(channel, epoch, work.completed, work.total);
    if (texts.isEmpty()) { finish(channel, epoch); return; }
    emit remoteNeeded(channel, epoch, texts);
}
void SearchWorker::acceptRemote(int channel, quint64 epoch, const QVariantList &vectors) {
    if (!current(channel, epoch)) return;
    struct QueryState &work = state(channel);
    if (work.epoch != epoch) return;
    if (vectors.size() != work.inFlight.size()) { emit failed(channel, epoch, QStringLiteral("SEARCH_RESPONSE: The provider returned an unexpected number of embeddings. Check model compatibility.")); return; }
    int dimensions = work.vectors.value(work.queryKey).size();
    for (qsizetype i = 0; i < vectors.size(); ++i) {
        const QVariantList values = vectors[i].toList();
        if (values.isEmpty() || values.size() > 16384 || (dimensions && values.size() != dimensions)) {
            emit failed(channel, epoch, QStringLiteral("SEARCH_DIMENSIONS: Embedding dimensions do not match. Use one compatible embedding model and retry.")); return;
        }
        dimensions = values.size();
        QVector<float> vector; vector.reserve(dimensions);
        double norm = 0;
        for (const QVariant &value : values) {
            bool ok = false; const double number = value.toDouble(&ok);
            if (!ok || !std::isfinite(number) || std::abs(number) > 1e10) { emit failed(channel, epoch, QStringLiteral("SEARCH_VECTOR: Provider returned an invalid embedding.")); return; }
            vector.append(float(number)); norm += number * number;
        }
        if (norm <= 1e-20) { emit failed(channel, epoch, QStringLiteral("SEARCH_VECTOR: Provider returned an empty embedding.")); return; }
        const double scale = std::sqrt(norm);
        for (float &value : vector) value = float(value / scale);
        const QString key = work.inFlight[i].key;
        if (!save(work.scope, key, vector)) { emit failed(channel, epoch, QStringLiteral("SEARCH_CACHE_WRITE: Cannot save embeddings. Check free space and retry.")); return; }
        work.vectors.insert(key, vector);
    }
    work.completed += vectors.size();
    emit progress(channel, epoch, work.completed, work.total);
    nextRemote(channel, epoch);
}
void SearchWorker::finish(int channel, quint64 epoch) {
    if (!current(channel, epoch)) return;
    const struct QueryState &work = state(channel);
    if (work.epoch != epoch) return;
    const auto query = work.vectors.value(work.queryKey);
    if (query.isEmpty()) { emit semanticReady(channel, epoch, {}); return; }
    QList<struct betterflash::search::SearchHit> hits;
    for (const struct betterflash::search::SearchDocument &doc : work.documents) {
        if (!current(channel, epoch)) return;
        if (work.textMatchIds.contains(doc.id)) continue;
        double best = -1;
        for (const QString &key : work.documentKeys.value(doc.id)) {
            const auto vector = work.vectors.value(key);
            if (vector.size() != query.size()) { emit failed(channel, epoch, QStringLiteral("SEARCH_DIMENSIONS: Cached embeddings have incompatible dimensions. Change the embedding model identifier or rebuild its cache.")); return; }
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
    // Deck membership includes every relevant card instead of a global top-card cutoff.
    if (channel == 0 && hits.size() > 12) hits.resize(12);
    emit semanticReady(channel, epoch, hitMaps(hits));
}
