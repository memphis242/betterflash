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
    Q_PROPERTY(bool testBusy READ testBusy NOTIFY changed)
    Q_PROPERTY(QString testStatus READ testStatus NOTIFY changed)
    Q_PROPERTY(QString testInput READ testInput NOTIFY changed)
    Q_PROPERTY(QString testResult READ testResult NOTIFY changed)
    Q_PROPERTY(QString testErrorCode READ testErrorCode NOTIFY changed)
    Q_PROPERTY(QString testErrorDetail READ testErrorDetail NOTIFY changed)
public:
    explicit WebSearch(ApiKeyRegistry *registry, QObject *parent = nullptr);
    ~WebSearch() override;
    bool enabled() const { return m_enabled; }
    void setEnabled(bool enabled);
    bool hasApiKey() const;
    QObject *apiKey() const;
    bool testBusy() const { return m_testBusy; }
    QString testStatus() const { return m_testStatus; }
    QString testInput() const { return m_testInput; }
    QString testResult() const { return m_testResult; }
    QString testErrorCode() const { return m_testErrorCode; }
    QString testErrorDetail() const { return m_testErrorDetail; }
    Q_INVOKABLE void testConnection();
    Q_INVOKABLE void cancelTest();
    using Completion = std::function<void(std::expected<QVariantList, struct ProviderError>)>;
    void search(const QString &query, QObject *context, Completion callback);
    void cancel();
signals:
    void changed();
    void configurationChanged();
private:
    void startSearch(const QString &query, QObject *context, Completion callback, bool requireEnabled);
    void clearTestResult();
    ApiKeyRegistry *const m_registry;
    WebSearch *m_testSearch = nullptr;
    bool m_testBusy = false;
    QString m_testStatus{};
    QString m_testInput{};
    QString m_testResult{};
    QString m_testErrorCode{};
    QString m_testErrorDetail{};
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
