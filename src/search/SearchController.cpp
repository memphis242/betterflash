#include "SearchController.h"
#include "SearchWorker.h"
#include "core/appcontroller.h"
#include "integrations/SecretStore.h"
#include "integrations/Summarizer.h"
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QSet>
#include <QStandardPaths>
#include <QUrl>
#include <utility>

namespace {
QString digest(const QString &value) {
    return QString::fromLatin1(QCryptographicHash::hash(value.toUtf8(), QCryptographicHash::Sha256).toHex());
}
bool sameOrigin(const QUrl &a, const QUrl &b) {
    return a.scheme() == b.scheme() && a.host() == b.host()
        && a.port(a.scheme() == QStringLiteral("https") ? 443 : 80)
            == b.port(b.scheme() == QStringLiteral("https") ? 443 : 80);
}
bool validEndpoint(const QUrl &url) {
    const bool loopback = url.host() == QStringLiteral("localhost") || url.host() == QStringLiteral("127.0.0.1") || url.host() == QStringLiteral("::1");
    return url.isValid() && !url.host().isEmpty() && url.userInfo().isEmpty() && !url.hasFragment() && !url.hasQuery()
        && (url.scheme() == QStringLiteral("https") || (loopback && url.scheme() == QStringLiteral("http")));
}
}

SearchController::SearchController(AppController *app, Summarizer *ai, const QString &dataDirectory, QObject *parent)
    : QObject(parent), m_app(app), m_ai(ai),
      m_modelRoot(qEnvironmentVariable("BETTERFLASH_MODEL_DIR", QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/BetterFlash/models"))) {
    Q_ASSERT(m_app && m_ai);
    m_enabled = m_settings.value(QStringLiteral("search/semanticEnabled"), true).toBool();
    m_provider = m_settings.value(QStringLiteral("search/provider"), QStringLiteral("local")).toString();
    if (m_provider != QStringLiteral("remote")) m_provider = QStringLiteral("local");
    m_profile = m_settings.value(QStringLiteral("search/localProfile"), QStringLiteral("automatic")).toString();
    if (m_profile != QStringLiteral("desktop") && m_profile != QStringLiteral("compact")) m_profile = QStringLiteral("automatic");
    m_endpoint = m_settings.value(QStringLiteral("search/endpoint")).toString();
    m_model = m_settings.value(QStringLiteral("search/model")).toString();
    m_reuseKey = m_settings.value(QStringLiteral("search/reuseLlmKey"), false).toBool();
    configureCredentials();
    static_assert(SearchWorker::channelCount == std::tuple_size_v<decltype(m_channels)>);
    m_worker = new SearchWorker(dataDirectory + QStringLiteral("/search/embeddings.sqlite"),
                                {m_channels[0].epoch, m_channels[1].epoch}, m_testEpoch);
    m_worker->moveToThread(&m_thread);
    connect(&m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    connect(m_worker, &SearchWorker::literalReady, this, [this](int channel, quint64 epoch, const QVariantList &hits) {
        struct QueryState &search = state(channel);
        if (epoch != search.epoch->load()) return;
        search.literal = hits; publish(channel);
    });
    connect(m_worker, &SearchWorker::fuzzyReady, this, [this](int channel, quint64 epoch, const QVariantList &hits) {
        struct QueryState &search = state(channel);
        if (epoch != search.epoch->load()) return;
        search.fuzzy = hits; search.lexicalComplete = true; publish(channel);
        if (m_enabled && !search.cards.isEmpty()) {
            search.status = QStringLiteral("Finding related cards"); search.semanticDelay.start();
        } else { search.busy = false; search.status.clear(); }
        notify(channel);
    });
    connect(m_worker, &SearchWorker::semanticReady, this, [this](int channel, quint64 epoch, const QVariantList &hits) {
        struct QueryState &search = state(channel);
        if (epoch != search.epoch->load()) return;
        search.semantic = hits; publish(channel); search.busy = false; search.status.clear(); notify(channel);
    });
    connect(m_worker, &SearchWorker::progress, this, [this](int channel, quint64 epoch, int complete, int total) {
        struct QueryState &search = state(channel);
        if (epoch != search.epoch->load()) return;
        search.status = total ? QStringLiteral("Preparing semantic search: %1 of %2 passages").arg(complete).arg(total)
                              : QStringLiteral("Finding related cards");
        notify(channel);
    });
    connect(m_worker, &SearchWorker::failed, this, [this](int channel, quint64 epoch, const QString &error) {
        if (epoch == state(channel).epoch->load()) setFailure(error, channel);
    });
    connect(m_worker, &SearchWorker::remoteNeeded, this, &SearchController::requestRemote);
    connect(m_worker, &SearchWorker::testFinished, this, [this](quint64 epoch, bool success, const QVariantMap &report, const QString &message) {
        if (epoch != m_testEpoch->load()) return;
        m_testBusy = false;
        m_testReport = report;
        m_testStatus = success ? message : QString();
        m_testError = success ? QString() : message;
        emit testChanged();
    });
    m_thread.setObjectName(QStringLiteral("library-search")); m_thread.start();
    for (int channel = 0; channel < int(m_channels.size()); ++channel) {
        struct QueryState &search = state(channel);
        search.debounce.setSingleShot(true); search.debounce.setInterval(25);
        search.semanticDelay.setSingleShot(true); search.semanticDelay.setInterval(180);
        connect(&search.debounce, &QTimer::timeout, this, [this, channel] { runSearch(channel); });
        connect(&search.semanticDelay, &QTimer::timeout, this, [this, channel] { runSemantic(channel); });
    }
    connect(m_app, &AppController::cardsChanged, this, [this] { restartAll(true); });
    connect(m_app, &AppController::decksChanged, this, [this] { restartAll(true); });
    connect(m_ai, &Summarizer::actualCredentialChanged, this, [this] {
        if (m_reuseKey && m_provider == QStringLiteral("remote")) { cancelTest(); resetTestReport(); restartAll(); }
    });
    connect(m_ai, &Summarizer::keyChanged, this, [this] {
        emit changed();
    });
    connect(m_ai, &Summarizer::configurationChanged, this, [this] {
        if (m_reuseKey && m_provider == QStringLiteral("remote")) { cancelTest(); resetTestReport(); restartAll(); }
    });
}
SearchController::~SearchController() {
    for (int channel = 0; channel < int(m_channels.size()); ++channel) {
        ++*state(channel).epoch; abortRemote(channel);
    }
    stopDownload();
    cancelTest();
    m_thread.quit(); m_thread.wait();
}
struct SearchController::QueryState &SearchController::state(int channel) {
    Q_ASSERT(channel >= 0 && channel < int(m_channels.size()));
    Q_ASSERT(QThread::currentThread() == thread());
    return m_channels[channel];
}
void SearchController::notify(int channel) {
    Q_ASSERT(channel >= 0 && channel < int(m_channels.size()));
    if (channel == 0) emit changed();
    else emit deckChanged();
}
QString SearchController::resolvedProfile() const {
    if (m_profile != QStringLiteral("automatic")) return m_profile;
#ifdef Q_OS_ANDROID
    return QStringLiteral("compact");
#else
    return QStringLiteral("desktop");
#endif
}
QString SearchController::modelLabel() const { return LocalEmbedder::spec(resolvedProfile()).label; }
QString SearchController::modelDirectory() const { return m_modelRoot + QLatin1Char('/') + LocalEmbedder::spec(resolvedProfile()).id; }
bool SearchController::modelReady() const {
    const struct LocalModelSpec spec = LocalEmbedder::spec(resolvedProfile());
    for (const struct ModelAsset &asset : spec.assets) {
        const QFileInfo file(modelDirectory() + QLatin1Char('/') + asset.filename);
        if (!file.isFile() || file.size() != asset.bytes) return false;
    }
    return !spec.assets.isEmpty();
}
bool SearchController::hasApiKey() const { return m_reuseKey ? m_ai->hasApiKey() : m_credentials && m_credentials->hasKey(); }
QString SearchController::keyStorageStatus() const { return m_reuseKey ? m_ai->keyStorageStatus() : m_credentials ? m_credentials->status() : QString(); }
QString SearchController::keyStorageState() const { return m_reuseKey ? m_ai->keyStorageState() : m_credentials ? m_credentials->storageState() : QStringLiteral("loading"); }
void SearchController::configureCredentials() {
    if (m_credentials) { m_credentials->disconnect(this); m_credentials->deleteLater(); }
    const QUrl url(m_endpoint);
    const QString scope = url.scheme() + QStringLiteral("://") + url.host() + QLatin1Char(':') + QString::number(url.port(url.scheme() == QStringLiteral("https") ? 443 : 80));
    m_credentials = new SecretStore(QStringLiteral("embeddings-") + digest(scope).left(24), {}, this);
    m_lastCredentialKey = m_credentials->key();
    connect(m_credentials, &SecretStore::changed, this, [this] {
        const QString key = m_credentials->key();
        if (key != m_lastCredentialKey) {
            m_lastCredentialKey = key;
            if (m_provider == QStringLiteral("remote")) { cancelTest(); resetTestReport(); }
        }
        emit changed(); if (m_provider == QStringLiteral("remote")) restartAll();
    });
}
void SearchController::setSemanticEnabled(bool enabled) {
    if (m_enabled == enabled) return;
    cancelTest(); resetTestReport();
    m_enabled = enabled; m_settings.setValue(QStringLiteral("search/semanticEnabled"), enabled);
    if (!enabled) stopDownload();
    restartAll(); emit changed();
}
void SearchController::setProvider(const QString &provider) {
    if (provider == m_provider || (provider != QStringLiteral("local") && provider != QStringLiteral("remote"))) return;
    cancelTest(); resetTestReport();
    m_provider = provider; m_settings.setValue(QStringLiteral("search/provider"), provider);
    stopDownload(); restartAll(); emit changed();
}
void SearchController::setLocalProfile(const QString &profile) {
    if (profile == m_profile || (profile != QStringLiteral("automatic") && profile != QStringLiteral("desktop") && profile != QStringLiteral("compact"))) return;
    cancelTest(); resetTestReport(); stopDownload(); m_profile = profile; m_downloadCanceled = false;
    m_settings.setValue(QStringLiteral("search/localProfile"), profile); restartAll(); emit changed();
}
bool SearchController::setRemoteConfiguration(const QString &endpoint, const QString &model, bool reuseKey, const QString &key) {
    const QString cleanEndpoint = endpoint.trimmed(); const QString cleanModel = model.trimmed();
    const QUrl url(cleanEndpoint);
    if (!validEndpoint(url)) { setFailure(QStringLiteral("SEARCH_ENDPOINT: Enter an HTTPS embeddings endpoint, or HTTP on localhost, without credentials or query parameters.")); return false; }
    if (cleanModel.isEmpty() || cleanModel.size() > 256) { setFailure(QStringLiteral("SEARCH_MODEL: Enter the provider's embedding model identifier.")); return false; }
    if (reuseKey && !sameOrigin(url, QUrl(m_ai->endpoint()))) { setFailure(QStringLiteral("SEARCH_KEY_ORIGIN: Reusing the language model key requires the same scheme, host and port. Enter a separate embedding key for this service.")); return false; }
    if (key.size() > 4096 || key.contains(QLatin1Char('\n')) || key.contains(QLatin1Char('\r'))) { setFailure(QStringLiteral("SEARCH_KEY: Enter a single API key without line breaks.")); return false; }
    const bool changedOrigin = !sameOrigin(url, QUrl(m_endpoint));
    m_endpoint = cleanEndpoint; m_model = cleanModel; m_reuseKey = reuseKey;
    m_settings.setValue(QStringLiteral("search/endpoint"), m_endpoint);
    m_settings.setValue(QStringLiteral("search/model"), m_model);
    m_settings.setValue(QStringLiteral("search/reuseLlmKey"), m_reuseKey);
    if (changedOrigin) configureCredentials();
    if (!reuseKey && !key.trimmed().isEmpty()) m_credentials->setKey(key);
    cancelTest(); resetTestReport();
    restartAll(); emit changed();
    return true;
}
void SearchController::clearApiKey() {
    if (m_provider == QStringLiteral("remote")) { cancelTest(); resetTestReport(); }
    m_credentials->clear();
}
void SearchController::rebuildIndex() {
    restartAll(true);
    for (struct QueryState &search : m_channels) search.debounce.stop();
    QMetaObject::invokeMethod(m_worker, [worker = m_worker] { worker->clearCache(); }, Qt::QueuedConnection);
    for (int channel = 0; channel < int(m_channels.size()); ++channel) {
        struct QueryState &search = state(channel);
        if (search.active && !search.query.trimmed().isEmpty()) search.debounce.start();
        search.status = QStringLiteral("Rebuilding the search cache"); notify(channel);
    }
}
void SearchController::resetTestReport() {
    m_testReport.clear();
    m_testStatus.clear();
    m_testError.clear();
    emit testChanged();
}

void SearchController::testModel() {
    cancelTest();
    const quint64 epoch = ++*m_testEpoch;
    m_testBusy = true;
    m_testReport = QVariantMap{{QStringLiteral("outcome"), QStringLiteral("running")},
        {QStringLiteral("input"), QStringLiteral("Multiplication combines equal groups of quantities.")},
        {QStringLiteral("expected"), QStringLiteral("The related sentence should be more similar than the unrelated sentence; vectors should be normalized.")}};
    m_testStatus = QStringLiteral("Testing model connection and embeddings");
    m_testError.clear();
    emit testChanged();
    if (m_provider != QStringLiteral("remote")) {
        if (!modelReady()) {
            m_testBusy = false;
            m_testReport.insert(QStringLiteral("outcome"), QStringLiteral("failed"));
            m_testReport.insert(QStringLiteral("details"), QStringLiteral("SEARCH_TEST_MODEL_MISSING: Download the selected local embedding model before testing it."));
            m_testError = QStringLiteral("SEARCH_TEST_MODEL_MISSING: Download the selected local embedding model before testing it.");
            m_testStatus.clear();
            emit testChanged();
            return;
        }
        QMetaObject::invokeMethod(m_worker, [worker = m_worker, epoch, profile = resolvedProfile(), directory = modelDirectory()] {
            worker->testLocal(epoch, profile, directory);
        }, Qt::QueuedConnection);
        return;
    }
    const QUrl endpoint(m_endpoint);
    if (!validEndpoint(endpoint) || m_model.isEmpty()) {
        m_testBusy = false; m_testStatus.clear();
        m_testError = QStringLiteral("SEARCH_TEST_ENDPOINT: Configure a valid embedding endpoint and model before testing.");
        m_testReport.insert(QStringLiteral("outcome"), QStringLiteral("failed"));
        m_testReport.insert(QStringLiteral("details"), m_testError);
        emit testChanged(); return;
    }
    QByteArray authorization;
    if (m_reuseKey) {
        if (m_ai->provider() != QStringLiteral("remote")) {
            m_testBusy = false; m_testStatus.clear();
            m_testError = QStringLiteral("SEARCH_TEST_KEY_PROVIDER: Reusing the language model key requires the language model provider to be remote.");
            m_testReport.insert(QStringLiteral("outcome"), QStringLiteral("failed"));
        m_testReport.insert(QStringLiteral("details"), m_testError);
        emit testChanged(); return;
        }
        if (!sameOrigin(endpoint, QUrl(m_ai->endpoint()))) {
            m_testBusy = false; m_testStatus.clear();
            m_testError = QStringLiteral("SEARCH_TEST_KEY_ORIGIN: The embedding endpoint must share the language model key's origin.");
            m_testReport.insert(QStringLiteral("outcome"), QStringLiteral("failed"));
        m_testReport.insert(QStringLiteral("details"), m_testError);
        emit testChanged(); return;
        }
        const auto request = m_ai->configuredRequest();
        if (request) authorization = request->rawHeader("Authorization");
    } else if (m_credentials->hasKey()) {
        authorization = "Bearer " + m_credentials->key().toUtf8();
    }
    if (authorization.isEmpty() && endpoint.scheme() != QStringLiteral("http")) {
        m_testBusy = false; m_testStatus.clear();
        m_testError = QStringLiteral("SEARCH_TEST_KEY: Configure an embedding API key before testing this endpoint.");
        m_testReport.insert(QStringLiteral("outcome"), QStringLiteral("failed"));
        m_testReport.insert(QStringLiteral("details"), m_testError);
        emit testChanged(); return;
    }
    QNetworkRequest request(endpoint);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    request.setTransferTimeout(30000);
    if (!authorization.isEmpty()) request.setRawHeader("Authorization", authorization);
    const QJsonArray input{
        QStringLiteral("Multiplication combines equal groups of quantities."),
        QStringLiteral("Multiplying numbers finds the total in repeated groups."),
        QStringLiteral("A recipe uses flour, water, yeast, and a warm oven.")};
    const QJsonObject payload{{QStringLiteral("model"), m_model}, {QStringLiteral("input"), input},
                              {QStringLiteral("encoding_format"), QStringLiteral("float")}};
    m_testBody.clear();
    m_testReport.insert(QStringLiteral("input"), input.at(0).toString());
    m_testReport.insert(QStringLiteral("expected"), QStringLiteral("The related sentence should be more similar than the unrelated sentence. Usable vectors are normalized locally for cosine comparison."));
    m_testReply = m_network.post(request, QJsonDocument(payload).toJson(QJsonDocument::Compact));
    QNetworkReply *const reply = m_testReply;
    reply->setReadBufferSize(1024 * 1024);
    QTimer::singleShot(30000, reply, [this, reply] {
        if (m_testReply == reply && !reply->isFinished()) reply->abort();
    });
    connect(reply, &QIODevice::readyRead, this, [this, reply] {
        if (m_testReply != reply) return;
        m_testBody.append(reply->readAll());
        if (m_testBody.size() > 1024 * 1024) reply->abort();
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, epoch, input] {
        if (m_testReply != reply) return;
        m_testReply = nullptr; reply->deleteLater();
        const QByteArray body = std::exchange(m_testBody, {});
        if (epoch != m_testEpoch->load()) return;
        const int code = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        auto fail = [this](const QString &message) {
            m_testBusy = false; m_testStatus.clear(); m_testError = message;
            m_testReport.insert(QStringLiteral("outcome"), QStringLiteral("failed"));
            m_testReport.insert(QStringLiteral("details"), message); emit testChanged();
        };
        if (reply->error() != QNetworkReply::NoError || code < 200 || code >= 300 || body.size() > 1024 * 1024) {
            fail(QStringLiteral("SEARCH_TEST_HTTP_%1: Check the endpoint, API key and model identifier.").arg(code)); return;
        }
        QJsonParseError parse;
        const QJsonArray data = QJsonDocument::fromJson(body, &parse).object().value(QStringLiteral("data")).toArray();
        if (parse.error != QJsonParseError::NoError || data.size() != 3) {
            fail(QStringLiteral("SEARCH_TEST_RESPONSE: The endpoint did not return three indexed embedding vectors.")); return;
        }
        std::array<QVector<float>, 3> vectors;
        QSet<int> indices;
        for (const QJsonValue &entry : data) {
            const QJsonObject object = entry.toObject();
            const QJsonValue position = object.value(QStringLiteral("index"));
            const int index = position.toInt(-1);
            if (!position.isDouble() || position.toDouble() != index || index < 0 || index >= 3 || indices.contains(index)) {
                fail(QStringLiteral("SEARCH_TEST_RESPONSE: The endpoint returned invalid vector indexes.")); return;
            }
            indices.insert(index);
            const QJsonArray values = object.value(QStringLiteral("embedding")).toArray();
            if (values.isEmpty() || values.size() > 16384) { fail(QStringLiteral("SEARCH_TEST_RESPONSE: The endpoint returned an invalid embedding vector.")); return; }
            QVector<float> vector; vector.reserve(values.size()); double norm = 0;
            for (const QJsonValue &value : values) {
                if (!value.isDouble() || !std::isfinite(value.toDouble())) { fail(QStringLiteral("SEARCH_TEST_RESPONSE: The endpoint returned a nonnumeric vector.")); return; }
                const float number = float(value.toDouble()); vector.append(number); norm += double(number) * number;
            }
            if (!std::isfinite(norm) || norm <= 1e-20) { fail(QStringLiteral("SEARCH_TEST_RESPONSE: The endpoint returned a zero vector.")); return; }
            const float scale = float(1.0 / std::sqrt(norm)); for (float &value : vector) value *= scale;
            vectors[static_cast<size_t>(index)] = std::move(vector);
        }
        if (vectors[1].size() != vectors[0].size() || vectors[2].size() != vectors[0].size()) {
            fail(QStringLiteral("SEARCH_TEST_DIMENSIONS: The endpoint returned inconsistent vector dimensions.")); return;
        }
        double self = 0, related = 0, unrelated = 0;
        for (qsizetype index = 0; index < vectors[0].size(); ++index) {
            self += double(vectors[0][index]) * vectors[0][index];
            related += double(vectors[0][index]) * vectors[1][index];
            unrelated += double(vectors[0][index]) * vectors[2][index];
        }
        QVariantMap relatedReport{{QStringLiteral("label"), QStringLiteral("Related text")}, {QStringLiteral("query"), input.at(0).toString()},
            {QStringLiteral("text"), input.at(1).toString()}, {QStringLiteral("similarity"), related}};
        QVariantMap unrelatedReport{{QStringLiteral("label"), QStringLiteral("Unrelated text")}, {QStringLiteral("query"), input.at(0).toString()},
            {QStringLiteral("text"), input.at(2).toString()}, {QStringLiteral("similarity"), unrelated}};
        m_testReport.insert(QStringLiteral("comparisons"), QVariantList{relatedReport, unrelatedReport});
        m_testReport.insert(QStringLiteral("output"), QStringLiteral("%1-dimensional vectors; normalized locally for cosine comparison").arg(vectors[0].size()));
        if (indices.size() != 3 || !std::isfinite(self) || std::abs(self - 1.0) > 0.01
            || !std::isfinite(related) || !std::isfinite(unrelated) || related <= unrelated) {
            fail(QStringLiteral("SEARCH_TEST_RESPONSE: The endpoint did not preserve related-text similarity.")); return;
        }
        m_testBusy = false;
        m_testReport.insert(QStringLiteral("outcome"), QStringLiteral("passed"));
        m_testStatus = QStringLiteral("Endpoint returned %1-dimensional vectors. Normalized locally; related-text similarity: %2; unrelated-text similarity: %3.")
            .arg(vectors[0].size()).arg(related, 0, 'f', 3).arg(unrelated, 0, 'f', 3);
        m_testError.clear(); emit testChanged();
    });
}

void SearchController::cancelTest() {
    ++*m_testEpoch;
    if (m_testReply) { m_testReply->disconnect(this); m_testReply->abort(); m_testReply->deleteLater(); m_testReply = nullptr; }
    m_testBody.clear();
    m_testAfterDownload = false;
    if (!m_testBusy) return;
    m_testBusy = false; m_testStatus = QStringLiteral("Model test canceled");
    m_testReport.insert(QStringLiteral("outcome"), QStringLiteral("canceled")); emit testChanged();
}
void SearchController::setActive(bool active) {
    struct QueryState &search = state(0);
    if (search.active == active) return;
    search.active = active; restart(0);
}
void SearchController::setSearch(const QString &deckId, const QString &query) {
    struct QueryState &search = state(0);
    if (search.deck == deckId && search.query == query) return;
    search.deck = deckId; search.query = query; restart(0, true);
}
void SearchController::setCardFilter(const QVariantList &cardIds, bool enabled) {
    QSet<QString> ids;
    if (enabled) {
        for (const QVariant &entry : cardIds) {
            const QString id = entry.toString();
            if (!id.isEmpty()) ids.insert(id);
        }
    }
    if (m_cardFilterEnabled == enabled && m_cardFilter == ids) return;
    m_cardFilterEnabled = enabled; m_cardFilter = ids; restart(0, true);
}
void SearchController::setDeckSearchActive(bool active) {
    struct QueryState &search = state(1);
    if (search.active == active) return;
    search.active = active; restart(1);
}
void SearchController::setDeckSearch(const QString &query) {
    struct QueryState &search = state(1);
    if (search.query == query) return;
    search.query = query; restart(1, true);
}
void SearchController::retryDeckSearch() {
    m_downloadCanceled = false; restart(1);
}
void SearchController::restartAll(bool clearResults) {
    for (int channel = 0; channel < int(m_channels.size()); ++channel) restart(channel, clearResults);
}
void SearchController::restart(int channel, bool clearResults) {
    struct QueryState &search = state(channel);
    ++*search.epoch; search.debounce.stop(); search.semanticDelay.stop(); abortRemote(channel);
    search.busy = false; search.lexicalComplete = false; search.semanticStarted = false;
    search.error.clear(); search.status.clear();
    search.literal.clear(); search.fuzzy.clear(); search.semantic.clear();
    if (clearResults) {
        if (channel == 0) emit resultsAboutToChange();
        search.results.clear();
        if (channel == 0) emit resultsChanged();
        else emit deckResultsChanged();
    }
    if (search.query.size() > 256) { setFailure(QStringLiteral("SEARCH_QUERY_LENGTH: Keep the search query to 256 characters or fewer."), channel); return; }
    if (search.active && !search.query.trimmed().isEmpty()) {
        search.busy = true; search.status = QStringLiteral("Finding text matches"); search.debounce.start();
    }
    notify(channel);
}
void SearchController::runSearch(int channel) {
    struct QueryState &search = state(channel);
    const quint64 epoch = search.epoch->load();
    const QVariantList snapshot = m_app->cards(); const QVariantList decks = m_app->decks();
    QVariantList cards;
    search.cards.clear();
    for (const QVariant &entry : snapshot) {
        const QVariantMap card = entry.toMap();
        const QString id = card.value(QStringLiteral("id")).toString();
        if (!search.deck.isEmpty() && card.value(QStringLiteral("deckId")).toString() != search.deck) continue;
        if (channel == 0 && m_cardFilterEnabled && !m_cardFilter.contains(id)) continue;
        search.cards.insert(id, card); cards.append(card);
    }
    QMetaObject::invokeMethod(m_worker, [worker = m_worker, channel, epoch, cards, decks, deck = search.deck, query = search.query] {
        worker->search(channel, epoch, cards, decks, deck, query);
    }, Qt::QueuedConnection);
}
void SearchController::publish(int channel) {
    struct QueryState &search = state(channel);
    static const QStringList tiers{QStringLiteral("exact"), QStringLiteral("prefix"), QStringLiteral("contains"), QStringLiteral("fuzzy"), QStringLiteral("semantic")};
    static const QStringList groups{QStringLiteral("Exact matches"), QStringLiteral("Prefix matches"), QStringLiteral("Contains your query"), QStringLiteral("Similar spelling"), QStringLiteral("Related meaning")};
    QVariantList next; QSet<QString> seen; int previousTier = -1;
    const QVariantList all = search.literal + search.fuzzy + search.semantic;
    for (const QVariant &entry : all) {
        const QVariantMap hit = entry.toMap(); const QString id = hit.value(QStringLiteral("id")).toString();
        const int tier = hit.value(QStringLiteral("tier")).toInt();
        if (seen.contains(id) || !search.cards.contains(id) || tier < 0 || tier >= tiers.size()) continue;
        seen.insert(id); QVariantMap card = search.cards.value(id);
        card.insert(QStringLiteral("searchTier"), tiers[tier]); card.insert(QStringLiteral("searchGroup"), groups[tier]);
        card.insert(QStringLiteral("searchScore"), hit.value(QStringLiteral("score")));
        card.insert(QStringLiteral("groupStart"), tier != previousTier); previousTier = tier;
        next.append(card);
    }
    if (channel == 0) emit resultsAboutToChange();
    search.results = next;
    if (channel == 0) emit resultsChanged();
    else emit deckResultsChanged();
}
void SearchController::runSemantic(int channel) {
    struct QueryState &search = state(channel);
    if (!search.active || !m_enabled || !search.lexicalComplete || search.semanticStarted || search.query.trimmed().isEmpty() || search.cards.isEmpty()) return;
    const bool remote = m_provider == QStringLiteral("remote");
    if (!remote && !modelReady()) {
        if (m_downloading) { search.status = QStringLiteral("Downloading semantic model: %1%").arg(qRound(m_downloadProgress * 100)); notify(channel); return; }
        if (m_downloadCanceled) { search.busy = false; search.status = QStringLiteral("Text matches only. Resume the model download in Search settings."); notify(channel); return; }
        prepareModel(); return;
    }
    search.busy = true; search.semanticStarted = true;
    const quint64 epoch = search.epoch->load();
    const QString profile = resolvedProfile(); const QString directory = modelDirectory();
    QString scope;
    if (remote) scope = QStringLiteral("remote-v1:") + digest(m_endpoint + QLatin1Char('\n') + m_model);
    else {
        const struct LocalModelSpec spec = LocalEmbedder::spec(profile);
        QString identity = spec.id;
        for (const struct ModelAsset &asset : spec.assets) identity += QString::fromLatin1(asset.sha256);
        scope = QStringLiteral("local-cls-v1:") + digest(identity);
    }
    QMetaObject::invokeMethod(m_worker, [worker = m_worker, channel, epoch, profile, directory, scope, remote] {
        worker->semantic(channel, epoch, profile, directory, scope, remote);
    }, Qt::QueuedConnection);
}
void SearchController::setFailure(const QString &message, int channel) {
    struct QueryState &search = state(channel);
    search.error = message; search.busy = false; search.status = QStringLiteral("Text matches remain available"); notify(channel);
}
void SearchController::setSharedFailure(const QString &message) {
    for (int channel = 0; channel < int(m_channels.size()); ++channel) setFailure(message, channel);
}
void SearchController::setDownloadStatus(const QString &message) {
    for (int channel = 0; channel < int(m_channels.size()); ++channel) {
        struct QueryState &search = state(channel);
        if ((search.busy && search.lexicalComplete && !search.semanticStarted)
            || (channel == 0 && (!search.active || search.query.trimmed().isEmpty()))) {
            search.status = message;
        }
        notify(channel);
    }
}
void SearchController::abortRemote(int channel) {
    struct QueryState &search = state(channel);
    if (search.remoteReply) {
        search.remoteReply->disconnect(this); search.remoteReply->abort(); search.remoteReply->deleteLater(); search.remoteReply = nullptr;
    }
    search.remoteBody.clear();
}
void SearchController::requestRemote(int channel, quint64 epoch, const QStringList &texts) {
    struct QueryState &search = state(channel);
    if (epoch != search.epoch->load() || m_provider != QStringLiteral("remote") || !m_enabled) return;
    const QUrl endpoint(m_endpoint);
    if (!validEndpoint(endpoint) || m_model.isEmpty()) { setFailure(QStringLiteral("SEARCH_ENDPOINT: Configure an embedding endpoint and model in Search settings."), channel); return; }
    QByteArray authorization;
    if (m_reuseKey) {
        if (m_ai->provider() != QStringLiteral("remote")) { setFailure(QStringLiteral("SEARCH_KEY_PROVIDER: Reusing the language model key requires the language model provider to be remote."), channel); return; }
        if (!sameOrigin(endpoint, QUrl(m_ai->endpoint()))) { setFailure(QStringLiteral("SEARCH_KEY_ORIGIN: The language model service changed. Configure a key for this embedding service in Search settings."), channel); return; }
        const auto request = m_ai->configuredRequest();
        if (!request) { setFailure(QStringLiteral("SEARCH_KEY: Configure the existing language model connection or provide a separate embedding key."), channel); return; }
        authorization = request->rawHeader("Authorization");
    } else if (m_credentials->hasKey()) authorization = "Bearer " + m_credentials->key().toUtf8();
    if (authorization.isEmpty() && endpoint.scheme() != QStringLiteral("http")) { setFailure(QStringLiteral("SEARCH_KEY: Add a key for the embedding service in Search settings."), channel); return; }
    QNetworkRequest request(endpoint);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    request.setTransferTimeout(45000);
    if (!authorization.isEmpty()) request.setRawHeader("Authorization", authorization);
    QJsonArray input; for (const QString &text : texts) input.append(text);
    const QJsonObject payload{{QStringLiteral("model"), m_model}, {QStringLiteral("input"), input}, {QStringLiteral("encoding_format"), QStringLiteral("float")}};
    search.remoteBody.clear(); search.remoteReply = m_network.post(request, QJsonDocument(payload).toJson(QJsonDocument::Compact));
    QNetworkReply *const reply = search.remoteReply;
    reply->setReadBufferSize(1024 * 1024);
    connect(reply, &QIODevice::readyRead, this, [this, reply, channel] {
        struct QueryState &search = state(channel);
        if (search.remoteReply != reply) return;
        search.remoteBody.append(reply->readAll());
        if (search.remoteBody.size() > 8 * 1024 * 1024) reply->abort();
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, channel, epoch, count = texts.size()] {
        struct QueryState &search = state(channel);
        if (search.remoteReply != reply) return;
        search.remoteBody.append(reply->readAll());
        const QByteArray body = std::exchange(search.remoteBody, {});
        search.remoteReply = nullptr; reply->deleteLater();
        if (epoch != search.epoch->load()) return;
        const int code = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (reply->error() != QNetworkReply::NoError || code < 200 || code >= 300 || body.size() > 8 * 1024 * 1024) {
            setFailure(QStringLiteral("SEARCH_HTTP_%1: %2").arg(code).arg(code == 401 || code == 403
                ? QStringLiteral("Check the embedding provider key and model access.") : code == 429
                ? QStringLiteral("Provider rate limit reached. Wait and retry.") : QStringLiteral("Check the embedding endpoint, model and connection, then retry.")), channel); return;
        }
        QJsonParseError parse;
        const QJsonDocument document = QJsonDocument::fromJson(body, &parse);
        const QJsonArray data = document.object().value(QStringLiteral("data")).toArray();
        if (parse.error != QJsonParseError::NoError || data.size() != count) { setFailure(QStringLiteral("SEARCH_RESPONSE: The provider did not return the expected embedding list. Use a compatible embeddings endpoint."), channel); return; }
        QVariantList vectors; vectors.resize(count); QSet<int> indices;
        for (const QJsonValue &entry : data) {
            const QJsonObject item = entry.toObject();
            const QJsonValue position = item.value(QStringLiteral("index"));
            const int index = position.toInt(-1); const QJsonArray values = item.value(QStringLiteral("embedding")).toArray();
            if (!position.isDouble() || position.toDouble() != index || index < 0 || index >= count || indices.contains(index) || values.isEmpty() || values.size() > 16384) { setFailure(QStringLiteral("SEARCH_RESPONSE: The provider returned malformed embeddings. Check model compatibility."), channel); return; }
            for (const QJsonValue &value : values) if (!value.isDouble()) { setFailure(QStringLiteral("SEARCH_RESPONSE: The provider returned nonnumeric embeddings."), channel); return; }
            indices.insert(index); vectors[index] = values.toVariantList();
        }
        QMetaObject::invokeMethod(m_worker, [worker = m_worker, channel, epoch, vectors] { worker->acceptRemote(channel, epoch, vectors); }, Qt::QueuedConnection);
    });
}
void SearchController::prepareModel() {
    if (m_downloading) return;
    m_downloadCanceled = false;
    for (struct QueryState &search : m_channels) search.error.clear();
    m_downloadDirectory = modelDirectory();
    if (!QDir().mkpath(m_downloadDirectory)) { setSharedFailure(QStringLiteral("SEARCH_MODEL_DIRECTORY: Cannot create the model directory. Check storage permissions.")); return; }
    m_downloadAssets = LocalEmbedder::spec(resolvedProfile()).assets;
    m_assetIndex = 0; m_downloadBytes = 0; m_downloadProgress = 0; m_downloading = true;
    downloadNext();
}
void SearchController::downloadNext() {
    if (m_assetIndex >= m_downloadAssets.size()) {
        m_downloading = false; m_downloadProgress = 1; setDownloadStatus(QStringLiteral("Local model ready"));
        if (m_testAfterDownload) {
            m_testAfterDownload = false;
            const quint64 epoch = m_testEpoch->load();
            m_testStatus = QStringLiteral("Testing model connection and embeddings");
            emit testChanged();
            QMetaObject::invokeMethod(m_worker, [worker = m_worker, epoch, profile = resolvedProfile(), directory = modelDirectory()] {
                worker->testLocal(epoch, profile, directory);
            }, Qt::QueuedConnection);
        }
        for (int channel = 0; channel < int(m_channels.size()); ++channel) runSemantic(channel);
        return;
    }
    const struct ModelAsset asset = m_downloadAssets[m_assetIndex];
    const QString path = m_downloadDirectory + QLatin1Char('/') + asset.filename;
    m_downloadFile = std::make_unique<QSaveFile>(path);
    if (!m_downloadFile->open(QIODevice::WriteOnly)) { downloadFailure(QStringLiteral("SEARCH_MODEL_WRITE: Cannot save the model. Check free space and retry.")); return; }
    m_downloadHash = std::make_unique<QCryptographicHash>(QCryptographicHash::Sha256);
    m_assetBytes = 0;
    QNetworkRequest request(asset.url);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setTransferTimeout(60000);
    m_downloadReply = m_network.get(request);
    QNetworkReply *const reply = m_downloadReply;
    reply->setReadBufferSize(1024 * 1024);
    setDownloadStatus(QStringLiteral("Downloading %1").arg(modelLabel()));
    connect(reply, &QIODevice::readyRead, this, [this, reply, asset] {
        if (m_downloadReply != reply) return;
        const QByteArray bytes = reply->readAll();
        m_assetBytes += bytes.size();
        if (m_assetBytes > asset.bytes || m_downloadFile->write(bytes) != bytes.size()) { reply->abort(); return; }
        m_downloadHash->addData(bytes);
        qint64 total = 0; for (const struct ModelAsset &part : m_downloadAssets) total += part.bytes;
        m_downloadProgress = total ? double(m_downloadBytes + m_assetBytes) / total : 0;
        setDownloadStatus(QStringLiteral("Downloading semantic model: %1%").arg(qRound(m_downloadProgress * 100)));
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, asset] {
        if (m_downloadReply != reply) return;
        m_downloadReply = nullptr; reply->deleteLater();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (reply->error() != QNetworkReply::NoError || status < 200 || status >= 300) { downloadFailure(QStringLiteral("SEARCH_MODEL_DOWNLOAD: Model download failed. Check the connection and free space, then retry.")); return; }
        if (m_assetBytes != asset.bytes || m_downloadHash->result().toHex() != asset.sha256.toLower()) { downloadFailure(QStringLiteral("SEARCH_MODEL_CHECKSUM: Downloaded model did not match its pinned checksum. Retry the download.")); return; }
        if (!m_downloadFile->commit()) { downloadFailure(QStringLiteral("SEARCH_MODEL_WRITE: Cannot finish saving the model. Check free space and retry.")); return; }
        m_downloadFile.reset(); m_downloadHash.reset();
        m_downloadBytes += m_assetBytes; ++m_assetIndex; downloadNext();
    });
}
void SearchController::stopDownload() {
    if (m_downloadReply) { m_downloadReply->disconnect(this); m_downloadReply->abort(); m_downloadReply->deleteLater(); m_downloadReply = nullptr; }
    m_downloadFile.reset(); m_downloadHash.reset(); m_downloading = false;
}
void SearchController::downloadFailure(const QString &message) {
    stopDownload(); m_downloadCanceled = true; setSharedFailure(message);
    if (m_testAfterDownload) {
        m_testAfterDownload = false; m_testBusy = false; m_testStatus.clear(); m_testError = message; emit testChanged();
    }
}
void SearchController::cancelDownload() {
    stopDownload(); m_downloadCanceled = true;
    if (m_testAfterDownload) {
        m_testAfterDownload = false; m_testBusy = false; m_testStatus.clear();
        m_testError = QStringLiteral("SEARCH_TEST_MODEL: Local model download canceled before testing."); emit testChanged();
    }
    for (int channel = 0; channel < int(m_channels.size()); ++channel) {
        struct QueryState &search = state(channel);
        search.busy = false; search.status = QStringLiteral("Download canceled. Text search is available."); notify(channel);
    }
}
void SearchController::retry() {
    m_downloadCanceled = false; restart(0);
    if (!state(0).active || state(0).query.trimmed().isEmpty()) prepareModel();
}
