#pragma once

#include "LocalEmbedder.h"
#include <QCryptographicHash>
#include <QNetworkAccessManager>
#include <QPointer>
#include <QSaveFile>
#include <QSettings>
#include <QSet>
#include <QThread>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>
#include <array>
#include <atomic>
#include <memory>

class AppController;
class Summarizer;
class SecretStore;
class SearchWorker;
class QNetworkReply;

class SearchController final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList results READ results NOTIFY resultsChanged)
    Q_PROPERTY(QVariantList deckResults READ deckResults NOTIFY deckResultsChanged)
    Q_PROPERTY(bool deckBusy READ deckBusy NOTIFY deckChanged)
    Q_PROPERTY(QString deckStatus READ deckStatus NOTIFY deckChanged)
    Q_PROPERTY(QString deckError READ deckError NOTIFY deckChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
    Q_PROPERTY(bool semanticEnabled READ semanticEnabled WRITE setSemanticEnabled NOTIFY changed)
    Q_PROPERTY(QString provider READ provider WRITE setProvider NOTIFY changed)
    Q_PROPERTY(QString localProfile READ localProfile WRITE setLocalProfile NOTIFY changed)
    Q_PROPERTY(QString resolvedProfile READ resolvedProfile NOTIFY changed)
    Q_PROPERTY(QString modelLabel READ modelLabel NOTIFY changed)
    Q_PROPERTY(bool modelReady READ modelReady NOTIFY changed)
    Q_PROPERTY(bool downloading READ downloading NOTIFY changed)
    Q_PROPERTY(double downloadProgress READ downloadProgress NOTIFY changed)
    Q_PROPERTY(QString remoteEndpoint READ remoteEndpoint NOTIFY changed)
    Q_PROPERTY(QString remoteModel READ remoteModel NOTIFY changed)
    Q_PROPERTY(bool useLanguageModelKey READ useLanguageModelKey NOTIFY changed)
    Q_PROPERTY(bool hasApiKey READ hasApiKey NOTIFY changed)
    Q_PROPERTY(QString keyStorageStatus READ keyStorageStatus NOTIFY changed)
    Q_PROPERTY(QString keyStorageState READ keyStorageState NOTIFY changed)
    Q_PROPERTY(bool testBusy READ testBusy NOTIFY testChanged)
    Q_PROPERTY(QString testStatus READ testStatus NOTIFY testChanged)
    Q_PROPERTY(QString testError READ testError NOTIFY testChanged)
    Q_PROPERTY(QVariantMap testReport READ testReport NOTIFY testChanged)
public:
    explicit SearchController(AppController *app, Summarizer *ai, const QString &dataDirectory, QObject *parent = nullptr);
    ~SearchController() override;
    QVariantList results() const { return m_channels[0].results; }
    bool busy() const { return m_channels[0].busy; }
    QString status() const { return m_channels[0].status; }
    QString error() const { return m_channels[0].error; }
    QVariantList deckResults() const { return m_channels[1].results; }
    bool deckBusy() const { return m_channels[1].busy; }
    QString deckStatus() const { return m_channels[1].status; }
    QString deckError() const { return m_channels[1].error; }
    bool semanticEnabled() const { return m_enabled; }
    QString provider() const { return m_provider; }
    QString localProfile() const { return m_profile; }
    QString resolvedProfile() const;
    QString modelLabel() const;
    bool modelReady() const;
    bool downloading() const { return m_downloading; }
    double downloadProgress() const { return m_downloadProgress; }
    QString remoteEndpoint() const { return m_endpoint; }
    QString remoteModel() const { return m_model; }
    bool useLanguageModelKey() const { return m_reuseKey; }
    bool hasApiKey() const;
    QString keyStorageStatus() const;
    QString keyStorageState() const;
    bool testBusy() const { return m_testBusy; }
    QString testStatus() const { return m_testStatus; }
    QString testError() const { return m_testError; }
    QVariantMap testReport() const { return m_testReport; }
    void setSemanticEnabled(bool enabled);
    void setProvider(const QString &provider);
    void setLocalProfile(const QString &profile);
    Q_INVOKABLE void setSearch(const QString &deckId, const QString &query);
    Q_INVOKABLE void setActive(bool active);
    Q_INVOKABLE void setCardFilter(const QVariantList &cardIds, bool enabled);
    Q_INVOKABLE void setDeckSearch(const QString &query);
    Q_INVOKABLE void setDeckSearchActive(bool active);
    Q_INVOKABLE void retryDeckSearch();
    Q_INVOKABLE void prepareModel();
    Q_INVOKABLE void cancelDownload();
    Q_INVOKABLE void retry();
    Q_INVOKABLE bool setRemoteConfiguration(const QString &endpoint, const QString &model, bool reuseKey, const QString &key);
    Q_INVOKABLE void clearApiKey();
    Q_INVOKABLE void rebuildIndex();
    Q_INVOKABLE void testModel();
    Q_INVOKABLE void cancelTest();
signals:
    void resultsAboutToChange();
    void resultsChanged();
    void changed();
    void deckResultsChanged();
    void deckChanged();
    void testChanged();
private:
    struct QueryState {
        const std::shared_ptr<std::atomic<quint64>> epoch = std::make_shared<std::atomic<quint64>>(0);
        QTimer debounce;
        QTimer semanticDelay;
        QPointer<QNetworkReply> remoteReply;
        QByteArray remoteBody;
        QString deck;
        QString query;
        QHash<QString, QVariantMap> cards;
        QVariantList literal;
        QVariantList fuzzy;
        QVariantList semantic;
        QVariantList results;
        bool active = false;
        bool busy = false;
        bool lexicalComplete = false;
        bool semanticStarted = false;
        QString status;
        QString error;
    };
    struct QueryState &state(int channel);
    void notify(int channel);
    void restart(int channel, bool clearResults = false);
    void restartAll(bool clearResults = false);
    void runSearch(int channel);
    void runSemantic(int channel);
    void publish(int channel);
    void setFailure(const QString &message, int channel = 0);
    void setSharedFailure(const QString &message);
    void setDownloadStatus(const QString &message);
    void abortRemote(int channel);
    void requestRemote(int channel, quint64 epoch, const QStringList &texts);
    void configureCredentials();
    QString modelDirectory() const;
    void downloadNext();
    void downloadFailure(const QString &message);
    void stopDownload();
    void resetTestReport();
    AppController *const m_app;
    Summarizer *const m_ai;
    SecretStore *m_credentials = nullptr;
    QSettings m_settings;
    const QString m_modelRoot;
    std::array<struct QueryState, 2> m_channels;
    QThread m_thread;
    SearchWorker *m_worker = nullptr;
    QNetworkAccessManager m_network;
    QPointer<QNetworkReply> m_downloadReply;
    QSet<QString> m_cardFilter;
    bool m_cardFilterEnabled = false;
    bool m_enabled = true;
    bool m_downloading = false;
    bool m_downloadCanceled = false;
    QString m_provider;
    QString m_profile;
    QString m_endpoint;
    QString m_model;
    bool m_reuseKey = false;
    QList<struct ModelAsset> m_downloadAssets;
    int m_assetIndex = 0;
    qint64 m_downloadBytes = 0;
    qint64 m_assetBytes = 0;
    double m_downloadProgress = 0;
    QString m_downloadDirectory;
    std::unique_ptr<QSaveFile> m_downloadFile;
    std::unique_ptr<QCryptographicHash> m_downloadHash;
    const std::shared_ptr<std::atomic<quint64>> m_testEpoch = std::make_shared<std::atomic<quint64>>(0);
    QPointer<QNetworkReply> m_testReply;
    QByteArray m_testBody;
    bool m_testBusy = false;
    QString m_testStatus;
    QString m_testError;
    QString m_lastCredentialKey;
    QVariantMap m_testReport;
    bool m_testAfterDownload = false;
};
