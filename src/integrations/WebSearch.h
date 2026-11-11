#pragma once

#include "Summarizer.h"
#include <QNetworkAccessManager>
#include <QPointer>
#include <QSettings>
#include <QVariantList>
#include <expected>
#include <functional>

class ApiKeyBinding;
class ApiKeyRegistry;
class QNetworkReply;

class WebSearch final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY changed)
    Q_PROPERTY(bool hasApiKey READ hasApiKey NOTIFY changed)
    Q_PROPERTY(QObject *apiKey READ apiKey CONSTANT)
public:
    explicit WebSearch(ApiKeyRegistry *registry, QObject *parent = nullptr);
    ~WebSearch() override;
    bool enabled() const { return m_enabled; }
    void setEnabled(bool enabled);
    bool hasApiKey() const;
    QObject *apiKey() const;
    using Completion = std::function<void(std::expected<QVariantList, struct ProviderError>)>;
    void search(const QString &query, QObject *context, Completion callback);
    void cancel();
signals:
    void changed();
    void configurationChanged();
private:
    QNetworkAccessManager m_network;
    QSettings m_settings;
    ApiKeyBinding *const m_credentials;
    QPointer<QNetworkReply> m_reply{};
    QPointer<QObject> m_context{};
    Completion m_callback{};
    QByteArray m_body{};
    QString m_credentialSnapshot{};
    bool m_enabled = false;
    bool m_timedOut = false;
    bool m_tooLarge = false;
};
