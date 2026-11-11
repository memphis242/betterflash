#pragma once

#include <QObject>
#include <QJsonArray>
#include <QJsonObject>
#include <QSet>
#include <QVariantList>
#include <QVariantMap>
#include <memory>
#include <atomic>

class AppController;
class Summarizer;
class WebSearch;
class CardChatStore;

class CardChat final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString cardId READ cardId NOTIFY changed)
    Q_PROPERTY(QString threadId READ threadId NOTIFY changed)
    Q_PROPERTY(QVariantList threads READ threads NOTIFY changed)
    Q_PROPERTY(QVariantList messages READ messages NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(QVariantMap errorInfo READ errorInfo NOTIFY changed)
    Q_PROPERTY(bool stale READ stale NOTIFY changed)
    Q_PROPERTY(QString contextNotice READ contextNotice NOTIFY changed)
    Q_PROPERTY(bool dirty READ dirty NOTIFY changed)
    Q_PROPERTY(bool pendingReply READ pendingReply NOTIFY changed)
public:
    CardChat(AppController *app, Summarizer *provider, WebSearch *search,
             const QString &dataDirectory, QObject *parent = nullptr);
    ~CardChat() override;
    QString cardId() const { return m_cardId; }
    QString threadId() const { return m_threadId; }
    QVariantList threads() const;
    QVariantList messages() const;
    bool busy() const { return m_busy; }
    QString status() const { return m_status; }
    QVariantMap errorInfo() const { return m_errorInfo; }
    bool stale() const;
    QString contextNotice() const { return m_contextNotice; }
    bool dirty() const { return m_dirty; }
    bool pendingReply() const;
    Q_INVOKABLE bool openFor(const QString &cardId);
    Q_INVOKABLE void close();
    Q_INVOKABLE bool newThread();
    Q_INVOKABLE bool selectThread(const QString &id);
    Q_INVOKABLE bool deleteThread(const QString &id);
    Q_INVOKABLE bool send(const QString &message);
    Q_INVOKABLE void retry();
    Q_INVOKABLE void cancel();
    Q_INVOKABLE bool discardPending();
    Q_INVOKABLE bool retrySave();
    Q_INVOKABLE bool reload();
    Q_INVOKABLE void discardUnsaved();
signals:
    void changed();
private:
    int threadIndex(const QString &id) const;
    QJsonObject currentThread() const;
    QVariantMap savedCard(const QString &id) const;
    QJsonObject snapshot(const QVariantMap &card) const;
    bool persist(const QJsonArray &threads);
    bool commitThreads(const QJsonArray &threads);
    void finish(const QString &answer);
    void startTurn();
    void requestTurn();
    void receiveTurn(const QJsonObject &message);
    void runTool(const QString &name, const QJsonObject &arguments,
                 const QJsonObject &assistant, const QString &toolId);
    void addCardEvidence(const QVariantList &cards, bool readCard,
                         const QJsonObject &assistant, const QString &toolId, const QString &query = {});
    void addEvidence(const QJsonObject &assistant, const QString &toolId,
                     const QJsonArray &items, const QVariantList &sources, bool truncated);
    void fail(const QString &code, const QString &message, const QString &action, const QString &detail = {});
    qsizetype contextLimit() const;
    AppController *const m_app;
    Summarizer *const m_provider;
    WebSearch *const m_search;
    std::unique_ptr<CardChatStore> m_store;
    std::shared_ptr<std::atomic_bool> m_retrievalCanceled{};
    QJsonArray m_threads{};
    QJsonArray m_durableThreads{};
    QJsonArray m_turnMessages{};
    QVariantList m_turnSources{};
    QSet<QString> m_usedSourceIds{};
    QString m_cardId{};
    QString m_threadId{};
    QString m_durableThreadId{};
    QString m_status{};
    QString m_contextNotice{};
    QVariantMap m_errorInfo{};
    quint64 m_generation = 0;
    quint64 m_job = 0;
    int m_turnCount = 0;
    int m_toolCount = 0;
    int m_webCount = 0;
    int m_sourceSequence = 0;
    bool m_busy = false;
    bool m_dirty = false;
    bool m_searchEnabled = false;
};
