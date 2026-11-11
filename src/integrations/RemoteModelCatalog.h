#pragma once

#include <QByteArray>
#include <QNetworkAccessManager>
#include <QPointer>
#include <QStringList>
#include <QSet>

class ApiKeyBinding;
class QNetworkReply;
class QUrl;

// Fetches the provider's model registry without exposing credentials to QML.
class RemoteModelCatalog : public QObject {
    Q_OBJECT
    Q_PROPERTY(QObject *apiKey READ apiKey WRITE setApiKey NOTIFY configurationChanged)
    Q_PROPERTY(QString endpoint READ endpoint WRITE setEndpoint NOTIFY configurationChanged)
    Q_PROPERTY(QString category READ category WRITE setCategory NOTIFY configurationChanged)
    Q_PROPERTY(QString remoteApi READ remoteApi WRITE setRemoteApi NOTIFY configurationChanged)
    Q_PROPERTY(QString workspaceId READ workspaceId WRITE setWorkspaceId NOTIFY configurationChanged)
    Q_PROPERTY(bool active READ active WRITE setActive NOTIFY configurationChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(QStringList models READ models NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
    Q_PROPERTY(QString categoryHint READ categoryHint NOTIFY changed)

public:
    explicit RemoteModelCatalog(QObject *parent = nullptr);
    ~RemoteModelCatalog() override;

    QObject *apiKey() const;
    QString endpoint() const;
    QString category() const;
    QString remoteApi() const;
    QString workspaceId() const;
    bool active() const;
    bool busy() const;
    QStringList models() const;
    QString status() const;
    QString error() const;
    QString categoryHint() const;

    void setApiKey(QObject *binding);
    void setEndpoint(const QString &endpoint);
    void setCategory(const QString &category);
    void setRemoteApi(const QString &remoteApi);
    void setWorkspaceId(const QString &workspaceId);
    void setActive(bool active);

    Q_INVOKABLE void fetch();
    Q_INVOKABLE void cancel();

signals:
    void configurationChanged();
    void changed();

private:
    void cancelRequest();
    void refreshCredentialSnapshot();
    void fail(const QString &code, const QString &message);
    bool prepareRequest(QUrl *modelsUrl, QByteArray *authorization);
    void finishReply(QNetworkReply *reply, quint64 generation);
    void fetchPage(const QString &afterId);

    QPointer<ApiKeyBinding> m_binding;
    QMetaObject::Connection m_bindingChangedConnection;
    QMetaObject::Connection m_bindingDestroyedConnection;
    QString m_endpoint;
    QString m_category;
    QString m_remoteApi = QStringLiteral("openai");
    QString m_workspaceId{};
    QStringList m_pendingModels{};
    QSet<QString> m_seenCursors{};
    int m_pageCount = 0;
    bool m_active = false;
    bool m_busy = false;
    QStringList m_models;
    QString m_status;
    QString m_error;
    QString m_categoryHint;
    QByteArray m_credentialDigest;
    QString m_credentialId;
    QNetworkAccessManager m_network;
    QPointer<QNetworkReply> m_reply;
    QByteArray m_body;
    bool m_responseTooLarge = false;
    quint64 m_generation = 0;
};
