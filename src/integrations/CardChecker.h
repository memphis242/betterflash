#pragma once

#include <QObject>
#include <QJsonArray>
#include <QJsonObject>
#include <QVariantMap>

class AppController;
class Summarizer;
class WebSearch;

class CardChecker final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(QString mode READ mode NOTIFY changed)
    Q_PROPERTY(QVariantMap errorInfo READ errorInfo NOTIFY changed)
    Q_PROPERTY(QVariantMap result READ result NOTIFY changed)
public:
    CardChecker(AppController *app, Summarizer *provider, WebSearch *search, QObject *parent = nullptr);
    ~CardChecker() override;
    bool busy() const { return m_busy; }
    QString status() const { return m_status; }
    QString mode() const { return m_mode; }
    QVariantMap errorInfo() const { return m_errorInfo; }
    QVariantMap result() const { return m_result; }
    Q_INVOKABLE void check(const QString &mode, const QString &front, const QString &back, const QString &kind,
                           bool allowWebSearch = true, const QString &cardId = {},
                           const QVariantMap &expectedSavedCard = {});
    Q_INVOKABLE void cancel();
    Q_INVOKABLE void clear();
signals:
    void changed();
private:
    bool validateContext();
    void requestTurn();
    void receiveTurn(const QJsonObject &message);
    void search(const QString &query, const QJsonObject &assistant, const QString &toolId);
    void finish(const QJsonObject &object);
    void fail(const QString &code, const QString &message, const QString &action, const QString &detail = {});
    AppController *const m_app;
    Summarizer *const m_provider;
    WebSearch *const m_search;
    quint64 m_job = 0;
    quint64 m_generation = 0;
    bool m_busy = false;
    bool m_searchEnabled = false;
    bool m_evidenceTruncated = false;
    bool m_formatting = false;
    int m_searchCount = 0;
    int m_turnCount = 0;
    QString m_initialFormatError{};
    QString m_pendingReset{};
    QString m_mode{};
    QString m_status{};
    QVariantMap m_errorInfo{};
    QVariantMap m_result{};
    QVariantList m_sources{};
    QJsonArray m_messages{};
};
