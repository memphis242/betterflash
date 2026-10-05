#pragma once

#include "SecretStore.h"
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QPointer>
#include <QSettings>
#include <expected>

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
    Q_PROPERTY(bool hasApiKey READ hasApiKey NOTIFY keyChanged)
    Q_PROPERTY(QString keyStorageStatus READ keyStorageStatus NOTIFY keyChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
    Q_PROPERTY(QString summary READ summary NOTIFY changed)
    Q_PROPERTY(int pendingCount READ pendingCount NOTIFY changed)
public:
    explicit Summarizer(AppController *app, QObject *parent = nullptr);
    ~Summarizer() override;
    QString endpoint() const { return m_endpoint; }
    QString model() const { return m_model; }
    bool hasApiKey() const { return m_credentials->hasKey(); }
    QString keyStorageStatus() const { return m_credentials->status(); }
    bool busy() const { return m_busy; }
    QString status() const { return m_status; }
    QString error() const { return m_error; }
    QString summary() const { return m_summary; }
    int pendingCount() const { return m_pendingCount; }
    void setEndpoint(const QString &endpoint);
    void setModel(const QString &model);
    std::expected<QNetworkRequest, struct ProviderError> configuredRequest() const;
    Q_INVOKABLE void setApiKey(const QString &key) { m_credentials->setKey(key); }
    Q_INVOKABLE void clearApiKey() { m_credentials->clear(); }
    Q_INVOKABLE void summarizeRemaining();
    Q_INVOKABLE void cancel();
signals:
    void configurationChanged();
    void keyChanged();
    void changed();
private:
    void fail(const QString &code, const QString &message);
    void configureCredentials();
    AppController *const m_app;
    SecretStore *m_credentials = nullptr;
    QNetworkAccessManager m_network;
    QPointer<QNetworkReply> m_reply;
    QSettings m_settings;
    QString m_endpoint;
    QString m_model;
    QString m_status;
    QString m_error;
    QString m_summary;
    bool m_busy = false;
    int m_pendingCount = 0;
};
