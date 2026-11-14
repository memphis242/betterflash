#pragma once

#include "ApiKeyBinding.h"
#include "models/ModelDownload.h"
#include <QHash>
#include <QJsonObject>
#include <QJsonArray>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QPointer>
#include <QSettings>
#include <QSet>
#include <QThreadPool>
#include <QVariantMap>
#include <expected>
#include <functional>

class AppController;
class QNetworkReply;
class ApiKeyRegistry;
class ModelResponseCache;

struct ProviderError {
    QString code;
    QString message;
    QString detail{};
    int httpStatus=0;
    QString providerCode{};
    QString rejectedGeneration{};
};

class Summarizer final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString endpoint READ endpoint WRITE setEndpoint NOTIFY configurationChanged)
    Q_PROPERTY(QString model READ model WRITE setModel NOTIFY configurationChanged)
    Q_PROPERTY(QString provider READ provider WRITE setProvider NOTIFY configurationChanged)
    Q_PROPERTY(QString remoteApi READ remoteApi WRITE setRemoteApi NOTIFY configurationChanged)
    Q_PROPERTY(QString workspaceId READ workspaceId WRITE setWorkspaceId NOTIFY configurationChanged)
    Q_PROPERTY(QString localProfile READ localProfile WRITE setLocalProfile NOTIFY configurationChanged)
    Q_PROPERTY(QString resolvedProfile READ resolvedProfile NOTIFY configurationChanged)
    Q_PROPERTY(QString localModelLabel READ localModelLabel NOTIFY configurationChanged)
    Q_PROPERTY(bool localModelReady READ localModelReady NOTIFY localChanged)
    Q_PROPERTY(bool localDownloading READ localDownloading NOTIFY localChanged)
    Q_PROPERTY(double localDownloadProgress READ localDownloadProgress NOTIFY localChanged)
    Q_PROPERTY(QString localStatus READ localStatus NOTIFY localChanged)
    Q_PROPERTY(QString localError READ localError NOTIFY localChanged)
    Q_PROPERTY(QString localErrorDetail READ localErrorDetail NOTIFY localChanged)
    Q_PROPERTY(QString acceleration READ acceleration WRITE setAcceleration NOTIFY configurationChanged)
    Q_PROPERTY(QString acceleratorStatus READ acceleratorStatus NOTIFY localChanged)
    Q_PROPERTY(bool testBusy READ testBusy NOTIFY testChanged)
    Q_PROPERTY(QString testStatus READ testStatus NOTIFY testChanged)
    Q_PROPERTY(QString testError READ testError NOTIFY testChanged)
    Q_PROPERTY(QVariantMap testReport READ testReport NOTIFY testChanged)
    Q_PROPERTY(bool hasApiKey READ hasApiKey NOTIFY keyChanged)
    Q_PROPERTY(QString keyStorageStatus READ keyStorageStatus NOTIFY keyChanged)
    Q_PROPERTY(QString keyStorageState READ keyStorageState NOTIFY keyChanged)
    Q_PROPERTY(QObject *apiKey READ apiKey CONSTANT)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
    Q_PROPERTY(QString errorDetail READ errorDetail NOTIFY changed)
    Q_PROPERTY(QString summary READ summary NOTIFY changed)
    Q_PROPERTY(QString summaryDeckId READ summaryDeckId NOTIFY changed)
    Q_PROPERTY(int pendingCount READ pendingCount NOTIFY changed)
    Q_PROPERTY(QVariantMap cacheErrorInfo READ cacheErrorInfo NOTIFY cacheChanged)
public:
    explicit Summarizer(AppController *app, QObject *parent = nullptr, ApiKeyRegistry *registry = nullptr,
                        const QString &dataDirectory = {});
    ~Summarizer() override;
    QString endpoint() const { return m_endpoint; }
    QString model() const { return m_model; }
    QString provider() const { return m_provider; }
    QString remoteApi() const { return m_remoteApi; }
    QString workspaceId() const { return m_workspaceId; }
    QString localProfile() const { return m_profile; }
    QString resolvedProfile() const;
    QString localModelLabel() const;
    bool localModelReady() const { return m_localDownload.ready(); }
    bool localDownloading() const { return m_localDownload.downloading(); }
    double localDownloadProgress() const { return m_localDownload.progress(); }
    QString localStatus() const { return m_localDownload.status(); }
    QString localError() const { return m_localDownload.error(); }
    QString localErrorDetail() const { return m_localDownload.errorDetail(); }
    QString acceleration() const { return m_acceleration; }
    QString acceleratorStatus() const { return m_acceleratorStatus; }
    bool testBusy() const { return m_testBusy; }
    QString testStatus() const { return m_testStatus; }
    QString testError() const { return m_testError; }
    QVariantMap testReport() const { return m_testReport; }
    bool hasApiKey() const { return m_credentials->hasKey(); }
    QString keyStorageStatus() const { return m_credentials->storageStatus(); }
    QString keyStorageState() const { return m_credentials->storageState(); }
    QObject *apiKey() const { return m_credentials; }
    ApiKeyRegistry *credentialRegistry() const;
    bool busy() const { return m_busy; }
    QString status() const { return m_status; }
    QString error() const { return m_error; }
    QString errorDetail() const { return m_errorDetail; }
    QString summary() const { return m_summary; }
    QString summaryDeckId() const { return m_summaryDeckId; }
    int pendingCount() const { return m_pendingCount; }
    [[nodiscard]] QVariantMap cacheErrorInfo() const { return m_cacheErrorInfo; }
    Q_INVOKABLE bool operationFresh(QObject *context) const;
    Q_INVOKABLE void setOperationFresh(QObject *context, bool fresh);
    Q_INVOKABLE QVariantMap operationCacheInfo(QObject *context) const;
    Q_INVOKABLE void acknowledgeCacheError();
    void setEndpoint(const QString &endpoint);
    void setModel(const QString &model);
    void setProvider(const QString &provider);
    void setRemoteApi(const QString &remoteApi);
    void setWorkspaceId(const QString &workspaceId);
    void setLocalProfile(const QString &profile);
    void setAcceleration(const QString &acceleration);
    Q_INVOKABLE void prepareLocalModel() { m_localDownload.start(); }
    Q_INVOKABLE void cancelLocalDownload() { m_localDownload.cancel(); }
    Q_INVOKABLE void testModel();
    Q_INVOKABLE void cancelTest();
    using Completion = std::function<void(std::expected<QString, struct ProviderError>)>;
    using MessageCompletion = std::function<void(std::expected<QJsonObject, struct ProviderError>)>;
    [[nodiscard]] quint64 complete(const QJsonObject &payload, QObject *context, Completion callback);
    [[nodiscard]] quint64 completeMessage(const QJsonObject &payload, QObject *context, MessageCompletion callback);
    void cancelCompletion(quint64 id);
    void acceptModelResponse(QObject *context);
    void beginDiagnosticOperation(QObject *context, QObject *policyContext = nullptr, bool aggregateCache = false,
                                  bool disableCache = false);
    QString logOperationFailure(QObject *context, const QString &code, const QString &message, const QString &detail = {},
                                bool responseRejected = true);
    std::expected<QNetworkRequest, struct ProviderError> configuredRequest() const;
    Q_INVOKABLE void setApiKey(const QString &key);
    Q_INVOKABLE void clearApiKey();
    Q_INVOKABLE void summarizeRemaining();
    Q_INVOKABLE void summarizeDeck(const QString &deckId);
    Q_INVOKABLE void cancel();
signals:
    void configurationChanged();
    void keyChanged();
    void actualCredentialChanged();
    void changed();
    void localChanged();
    void testChanged();
    void cachePolicyChanged();
    void cacheChanged();
private:
    void fail(const QString &code, const QString &message, const QString &detail = {});
    void summarizePending(const QVariantList &pending, const QString &scopeDeckId, const QString &scopeName);
    void configureCredentials();
    void startCompletion(quint64 id);
    void finishCompletion(quint64 id, std::expected<QJsonObject, struct ProviderError> result);
    void cancelAllCompletions();
    void resetTestReport();
    void ensureCacheContext(QObject *context);
    void reportCacheError(const QVariantMap &error);
    void discardCachedResponse(const QByteArray &key, const QByteArray &token);
    void storeCachedResponse(const QByteArray &key, const QJsonObject &message);
    [[nodiscard]] QByteArray completionCacheKey(const QJsonObject &effectivePayload, const QByteArray &assetDigest) const;
    struct CacheStats {
        bool forceFresh = false;
        int requests = 0;
        int hits = 0;
    };
    struct DiagnosticJournal {
        QString operationId{};
        QJsonArray entries{};
        qsizetype bytes = 0;
        bool truncated = false;
        bool disableCache = false;
        quint64 failureCount = 0;
        std::shared_ptr<struct CacheStats> cacheStats{};
    };
    void appendDiagnostic(const std::shared_ptr<struct DiagnosticJournal> &journal, const QJsonObject &entry);
    QString logDiagnosticJournal(const std::shared_ptr<struct DiagnosticJournal> &journal,
                                 const QString &code, const QString &message, const QString &detail);
    struct CompletionJob {
        QJsonObject payload{};
        std::shared_ptr<struct DiagnosticJournal> journal{};
        quint64 id = 0;
        QPointer<QObject> context{};
        MessageCompletion callback{};
        QPointer<QNetworkReply> reply{};
        QByteArray body{};
        QMetaObject::Connection contextConnection{};
        const std::shared_ptr<std::atomic_bool> canceled = std::make_shared<std::atomic_bool>(false);
        bool waitingForDownload = false;
        bool timedOut = false;
        bool anthropic = false;
        bool cacheChecked = false;
        bool cacheLookupPending = false;
        bool cacheHit = false;
        bool responseCacheable = false;
        bool acceptedByConsumer = false;
        bool assetFingerprintPending = false;
        bool cacheDisabledForJob = false;
        int assetChanges = 0;
        QByteArray assetIdentity{};
        QByteArray assetDigest{};
        QString assetPath{};
        QByteArray cacheKey{};
        QByteArray cacheToken{};
    };
    AppController *const m_app;
    ApiKeyBinding *m_credentials = nullptr;
    ApiKeyRegistry *const m_registry;
    QNetworkAccessManager m_network;
    QSettings m_settings;
    ModelDownload m_localDownload;
    QThreadPool m_inferencePool{};
    QThreadPool m_cachePool{};
    std::shared_ptr<ModelResponseCache> m_responseCache{};
    QHash<QObject *, bool> m_freshPreferences{};
    QSet<QObject *> m_cacheContexts{};
    QVariantMap m_cacheErrorInfo{};
    QList<QVariantMap> m_pendingCacheErrors{};
    QHash<quint64, std::shared_ptr<struct CompletionJob>> m_jobs{};
    QHash<QObject *, QList<std::shared_ptr<struct CompletionJob>>> m_responseCallbacks{};
    QHash<QObject *, std::shared_ptr<struct DiagnosticJournal>> m_diagnosticJournals{};
    QHash<QObject *, std::shared_ptr<struct DiagnosticJournal>> m_visibleCacheJournals{};
    QObject m_testContext{};
    QObject m_summaryContext{};
    quint64 m_jobSequence = 0;
    quint64 m_summaryJob = 0;
    quint64 m_testJob = 0;
    QString m_provider = QStringLiteral("local");
    QString m_remoteApi = QStringLiteral("openai");
    QString m_workspaceId{};
    QString m_profile = QStringLiteral("automatic");
    QString m_acceleration = QStringLiteral("automatic");
    QString m_acceleratorStatus = QStringLiteral("Device is checked when the model runs");
    bool m_testBusy = false;
    QString m_testStatus{};
    QString m_testError{};
    QVariantMap m_testReport{};
    QString m_endpoint;
    QString m_model;
    QString m_status;
    QString m_error;
    QString m_errorDetail{};
    QString m_lastKey;
    QString m_lastCredentialId;
    QString m_summary;
    QString m_summaryDeckId;
    bool m_busy = false;
    int m_pendingCount = 0;
};
