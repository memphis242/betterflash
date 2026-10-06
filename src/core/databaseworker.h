#pragma once

#include "cardmodel.h"

#include <QObject>
#include <QHash>
#include <QSet>
#include <QSqlDatabase>
#include <QStringList>
#include <QVariantList>
#include <functional>
#include <optional>

class DatabaseWorker final : public QObject {
    Q_OBJECT
public:
    explicit DatabaseWorker(QString path);
    ~DatabaseWorker() override;
    void initialize();
    void shutdown();
    void snapshot();
    void createDeck(const QString &name, const QString &description, const QString &parentId);
    void updateDeck(const QString &id, const QString &name, const QString &description, const QString &parentId);
    void deleteDeck(const QString &id);
    void saveCard(const QString &id, const QString &deckId, const QString &kind,
                  const QString &front, const QString &back, const QString &tags, int points);
    void deleteCard(const QString &id);
    void replaceCardWithAtomicCards(const QVariantMap &expectedSource,const QVariantList &proposals);
    void beginReview(const QString &deckId);
    void selectReviewCard(const QString &variantId);
    void endReview();
    void defer();
    void postpone(const QString &date);
    void gradeCard(int grade, double recall, double seconds, const QString &expectedVariant);
    void exportCollection(const QString &path);
    void importCollection(const QString &path);
    void loadExample();
    void requestSyncBatch(const QString &requestId);
    void applySyncResponse(const QVariantMap &response);

signals:
    void snapshotReady(const QVariantList &decks, const QVariantList &cards, const QVariantList &history);
    void operationFinished(bool ok, const QString &message, const QVariantMap &error);
    void reviewQueueReady(const QVariantList &cards, int total, bool resetCurrent, const QVariantMap &selectedCard,
                          bool inspecting, const QString &cursorVariantId);
    void gradeCommitted();
    void atomicSplitApplied(const QString &sourceId,bool success,const QStringList &newIds);
    void syncBatchReady(const QVariantMap &batch);
    void syncBatchFailed(const QString &code, const QString &message);
    void syncBatchFailedForRequest(const QString &requestId,const QString &code,const QString &message);
    void syncResponseApplied(const QString &requestId,bool success,bool hasMore);
    void syncApplied(bool success, bool hasMore);

private:
    QString m_path;
    QString m_connectionName;
    QSqlDatabase m_db;
    QString m_deviceId;
    qint64 m_cursor = 0;
    struct SyncRequest {
        QSet<QString> submittedIds;
        qint64 cursor=0;
    };
    QHash<QString,struct SyncRequest> m_requests;
    QStringList m_requestOrder;
    QStringList m_queue;
    QHash<QString,QVariantMap> m_queueCache;
    QSet<QString> m_sessionReviewedVariants;
    QString m_selectedVariant;
    QString m_inspectedVariant;
    bool m_queueDirty = true;
    int m_sessionTotal = 0;
    bool m_sessionActive = false;
    QString m_sqlDetail;

    void assertThread() const;
    bool ready();
    bool execute(const QString &sql, const QVariantList &parameters = {});
    bool mutate(const std::function<bool()> &work);
    bool enqueue(const QString &type, const QVariantMap &payload);
    bool validateMedia(const struct betterflash::model::Card &record);
    void fail(const QString &code, const QString &message, const QString &detail = {});
    void done(const QString &message = {});
    bool publishSnapshot();
    bool publishQueue(bool resetCurrent);
    void advanceAfterRemoval(qsizetype position);
    std::optional<struct betterflash::model::Deck> deck(const QString &id);
    std::optional<struct betterflash::model::Card> card(const QString &id);
    std::optional<struct betterflash::model::Variant> variant(const QString &id);
    QList<struct betterflash::model::Variant> cardVariants(const QString &id);
    bool upsertDeck(const struct betterflash::model::Deck &record);
    bool upsertCard(const struct betterflash::model::Card &record);
    bool upsertVariant(const struct betterflash::model::Variant &record);
    bool insertReview(const struct betterflash::model::Review &record);
    bool replaceVariants(const struct betterflash::model::Card &record,
                         const QList<struct betterflash::model::Variant> &variants);
    bool tombstone(const QString &type, const QString &id, qint64 seq = 0);
    bool isDeleted(const QString &type, const QString &id);
    bool eraseCard(const QString &id, qint64 seq = 0);
    bool eraseDeck(const QString &id, qint64 seq = 0);
    bool validDeckParent(const QString &id, const QString &parentId);
    QVariantMap cardPayload(const struct betterflash::model::Card &record);
    QVariantMap reviewCard(const struct betterflash::model::Card &record,
                           const struct betterflash::model::Variant &variant, const QString &deckName);
    std::optional<QVariantMap> reviewCardByVariant(const QString &variantId);
    bool migrate();
    bool applyRemoteEvent(const QVariantMap &event, qint64 seq, bool &applied);
    bool retryRemoteEvents();
    bool pendingEntity(const QString &type, const QString &id);
    qint64 entitySequence(const QString &type, const QString &id);
    bool recordSequence(const QString &type, const QString &id, qint64 seq);
    bool eventEffects(const QVariantMap &event, qint64 seq, bool executeEffects, bool &applied);
    bool importRecords(const struct betterflash::model::Collection &records);
};
