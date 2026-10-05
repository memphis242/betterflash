#pragma once

#include "SecretStore.h"
#include <QNetworkAccessManager>
#include <QPointer>
#include <QSettings>
#include <QVariantMap>

class AppController;
class QNetworkReply;

class SyncController final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString endpoint READ endpoint WRITE setEndpoint NOTIFY configurationChanged)
    Q_PROPERTY(bool hasToken READ hasToken NOTIFY keyChanged)
    Q_PROPERTY(QString keyStorageStatus READ keyStorageStatus NOTIFY keyChanged)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
    Q_PROPERTY(QString lastSync READ lastSync NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
public:
    explicit SyncController(AppController *app, const QString &mediaDirectory, QObject *parent = nullptr);
    QString endpoint() const { return m_endpoint; }
    bool hasToken() const { return m_credentials->hasKey(); }
    QString keyStorageStatus() const { return m_credentials->status(); }
    QString status() const { return m_status; }
    QString error() const { return m_error; }
    QString lastSync() const { return m_lastSync; }
    bool busy() const { return m_busy; }
    void setEndpoint(const QString &endpoint);
    Q_INVOKABLE void setToken(const QString &token) { m_credentials->setKey(token); }
    Q_INVOKABLE void clearToken() { m_credentials->clear(); }
    Q_INVOKABLE void synchronize();
    Q_INVOKABLE void cancel();
signals:
    void configurationChanged();
    void keyChanged();
    void changed();
private:
    void handleBatch(const QVariantMap &batch);
    void uploadNext();
    void postBatch();
    void downloadNext();
    void finishApply(bool success, bool hasMore);
    void fail(const QString &code, const QString &message);
    void configureCredentials();
    QUrl mediaUrl(const QString &name) const;
    QNetworkRequest request(const QUrl &url) const;
    AppController *const m_app;
    const QString m_mediaDirectory;
    SecretStore *m_credentials = nullptr;
    QNetworkAccessManager m_network;
    QPointer<QNetworkReply> m_reply;
    QSettings m_settings;
    QString m_endpoint;
    QString m_status = QStringLiteral("Stored on this device.");
    QString m_error;
    QString m_lastSync;
    QVariantMap m_batch;
    QVariantMap m_response;
    QStringList m_uploads;
    QStringList m_downloads;
    bool m_busy = false;
    bool m_batchWasFull = false;
    int m_rounds = 0;
};

