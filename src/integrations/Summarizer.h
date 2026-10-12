#pragma once

#include "SecretStore.h"
#include "models/ModelDownload.h"
#include <QHash>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QPointer>
#include <QSettings>
#include <QThreadPool>
#include <expected>
#include <functional>

class AppController;
class QNetworkReply;

struct ProviderError {
    QString code;
    QString message;
};

class Summarizer final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString endpoint READ endpoint WRITE setEndpoint NOTIFY configurationChanged)
    Q_PROPERTY(QString model READ model WRITE setModel NOTIFY configurationChanged)
    Q_PROPERTY(QString provider READ provider WRITE setProvider NOTIFY configurationChanged)
    Q_PROPERTY(QString localProfile READ localProfile WRITE setLocalProfile NOTIFY configurationChanged)
    Q_PROPERTY(QString resolvedProfile READ resolvedProfile NOTIFY configurationChanged)
    Q_PROPERTY(QString localModelLabel READ localModelLabel NOTIFY configurationChanged)
    Q_PROPERTY(bool localModelReady READ localModelReady NOTIFY localChanged)
    Q_PROPERTY(bool localDownloading READ localDownloading NOTIFY localChanged)
    Q_PROPERTY(double localDownloadProgress READ localDownloadProgress NOTIFY localChanged)
    Q_PROPERTY(QString localStatus READ localStatus NOTIFY localChanged)
    Q_PROPERTY(QString localError READ localError NOTIFY localChanged)
    Q_PROPERTY(QString acceleration READ acceleration WRITE setAcceleration NOTIFY configurationChanged)
    Q_PROPERTY(QString acceleratorStatus READ acceleratorStatus NOTIFY localChanged)
    Q_PROPERTY(bool testBusy READ testBusy NOTIFY testChanged)
    Q_PROPERTY(QString testStatus READ testStatus NOTIFY testChanged)
    Q_PROPERTY(QString testError READ testError NOTIFY testChanged)
    Q_PROPERTY(bool hasApiKey READ hasApiKey NOTIFY keyChanged)
    Q_PROPERTY(QString keyStorageStatus READ keyStorageStatus NOTIFY keyChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
    Q_PROPERTY(QString summary READ summary NOTIFY changed)
    Q_PROPERTY(QString summaryDeckId READ summaryDeckId NOTIFY changed)
    Q_PROPERTY(int pendingCount READ pendingCount NOTIFY changed)
public:
    explicit Summarizer(AppController *app, QObject *parent = nullptr);
    ~Summarizer() override;
    QString endpoint() const { return m_endpoint; }
    QString model() const { return m_model; }
    QString provider() const { return m_provider; }
    QString localProfile() const { return m_profile; }
    QString resolvedProfile() const;
    QString localModelLabel() const;
    bool localModelReady() const { return m_localDownload.ready(); }
    bool localDownloading() const { return m_localDownload.downloading(); }
    double localDownloadProgress() const { return m_localDownload.progress(); }
    QString localStatus() const { return m_localDownload.status(); }
    QString localError() const { return m_localDownload.error(); }
    QString acceleration() const { return m_acceleration; }
    QString acceleratorStatus() const { return m_acceleratorStatus; }
    bool testBusy() const { return m_testBusy; }
    QString testStatus() const { return m_testStatus; }
    QString testError() const { return m_testError; }
    bool hasApiKey() const { return m_credentials->hasKey(); }
    QString keyStorageStatus() const { return m_credentials->status(); }
    bool busy() const { return m_busy; }
    QString status() const { return m_status; }
    QString error() const { return m_error; }
    QString summary() const { return m_summary; }
    QString summaryDeckId() const { return m_summaryDeckId; }
    int pendingCount() const { return m_pendingCount; }
    void setEndpoint(const QString &endpoint);
    void setModel(const QString &model);
    void setProvider(const QString &provider);
    void setLocalProfile(const QString &profile);
    void setAcceleration(const QString &acceleration);
    Q_INVOKABLE void prepareLocalModel() { m_localDownload.start(); }
    Q_INVOKABLE void cancelLocalDownload() { m_localDownload.cancel(); }
    Q_INVOKABLE void testModel();
    Q_INVOKABLE void cancelTest();
    using Completion = std::function<void(std::expected<QString, struct ProviderError>)>;
    [[nodiscard]] quint64 complete(const QJsonObject &payload, QObject *context, Completion callback);
    void cancelCompletion(quint64 id);
    std::expected<QNetworkRequest, struct ProviderError> configuredRequest() const;
    Q_INVOKABLE void setApiKey(const QString &key) { m_credentials->setKey(key); }
    Q_INVOKABLE void clearApiKey() { m_credentials->clear(); }
    Q_INVOKABLE void summarizeRemaining();
    Q_INVOKABLE void summarizeDeck(const QString &deckId);
    Q_INVOKABLE void cancel();
signals:
    void configurationChanged();
    void keyChanged();
    void changed();
    void localChanged();
    void testChanged();
private:
    void fail(const QString &code, const QString &message);
    void summarizePending(const QVariantList &pending, const QString &scopeDeckId, const QString &scopeName);
    void configureCredentials();
    void startCompletion(quint64 id);
    void finishCompletion(quint64 id, std::expected<QString, struct ProviderError> result);
    void cancelAllCompletions();
    struct CompletionJob {
        QJsonObject payload{};
        QPointer<QObject> context{};
        Completion callback{};
        QPointer<QNetworkReply> reply{};
        QByteArray body{};
        QMetaObject::Connection contextConnection{};
        const std::shared_ptr<std::atomic_bool> canceled = std::make_shared<std::atomic_bool>(false);
        bool waitingForDownload = false;
    };
    AppController *const m_app;
    SecretStore *m_credentials = nullptr;
    QNetworkAccessManager m_network;
    QSettings m_settings;
    ModelDownload m_localDownload;
    QThreadPool m_inferencePool{};
    QHash<quint64, std::shared_ptr<struct CompletionJob>> m_jobs{};
    quint64 m_jobSequence = 0;
    quint64 m_summaryJob = 0;
    quint64 m_testJob = 0;
    QString m_provider = QStringLiteral("local");
    QString m_profile = QStringLiteral("automatic");
    QString m_acceleration = QStringLiteral("automatic");
    QString m_acceleratorStatus = QStringLiteral("Device is checked when the model runs");
    bool m_testBusy = false;
    QString m_testStatus{};
    QString m_testError{};
    QString m_endpoint;
    QString m_model;
    QString m_status;
    QString m_error;
    QString m_summary;
    QString m_summaryDeckId;
    bool m_busy = false;
    int m_pendingCount = 0;
};
