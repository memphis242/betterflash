#pragma once

#include "LocalEmbedder.h"
#include <QCryptographicHash>
#include <QNetworkAccessManager>
#include <QPointer>
#include <QSaveFile>
#include <QSettings>
#include <QThread>
#include <QTimer>
#include <QVariantList>
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
public:
    explicit SearchController(AppController *app, Summarizer *ai, const QString &dataDirectory, QObject *parent = nullptr);
    ~SearchController() override;
    QVariantList results() const { return m_results; }
    bool busy() const { return m_busy; }
    QString status() const { return m_status; }
    QString error() const { return m_error; }
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
    void setSemanticEnabled(bool enabled);
    void setProvider(const QString &provider);
    void setLocalProfile(const QString &profile);
    Q_INVOKABLE void setSearch(const QString &deckId, const QString &query);
    Q_INVOKABLE void setActive(bool active);
    Q_INVOKABLE void prepareModel();
    Q_INVOKABLE void cancelDownload();
    Q_INVOKABLE void retry();
    Q_INVOKABLE bool setRemoteConfiguration(const QString &endpoint, const QString &model, bool reuseKey, const QString &key);
    Q_INVOKABLE void clearApiKey();
    Q_INVOKABLE void rebuildIndex();
signals:
    void resultsAboutToChange();
    void resultsChanged();
    void changed();
private:
    void restart(bool clearResults = false);
    void runSearch();
    void runSemantic();
    void publish();
    void setFailure(const QString &message);
    void abortRemote();
    void requestRemote(quint64 epoch, const QStringList &texts);
    void configureCredentials();
    QString modelDirectory() const;
    void downloadNext();
    void downloadFailure(const QString &message);
    void stopDownload();
    AppController *const m_app;
    Summarizer *const m_ai;
    SecretStore *m_credentials = nullptr;
    QSettings m_settings;
    const QString m_modelRoot;
    const std::shared_ptr<std::atomic<quint64>> m_epoch;
    QThread m_thread;
    SearchWorker *m_worker = nullptr;
    QTimer m_debounce;
    QTimer m_semanticDelay;
    QNetworkAccessManager m_network;
    QPointer<QNetworkReply> m_remoteReply;
    QPointer<QNetworkReply> m_downloadReply;
    QByteArray m_remoteBody;
    QString m_deck;
    QString m_query;
    QHash<QString, QVariantMap> m_cards;
    QVariantList m_literal;
    QVariantList m_fuzzy;
    QVariantList m_semantic;
    QVariantList m_results;
    bool m_active = false;
    bool m_busy = false;
    bool m_enabled = true;
    bool m_downloading = false;
    bool m_downloadCanceled = false;
    bool m_lexicalComplete = false;
    bool m_semanticStarted = false;
    QString m_status;
    QString m_error;
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
};
