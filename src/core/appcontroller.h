#pragma once

#include <QElapsedTimer>
#include <QObject>
#include <QStringList>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>
#include <QSet>
#include <functional>

class QThread;
class DatabaseWorker;

class AppController final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList decks READ decks NOTIFY decksChanged)
    Q_PROPERTY(QVariantList cards READ cards NOTIFY cardsChanged)
    Q_PROPERTY(QVariantList history READ history NOTIFY historyChanged)
    Q_PROPERTY(QString selectedDeckId READ selectedDeckId WRITE setSelectedDeckId NOTIFY selectedDeckIdChanged)
    Q_PROPERTY(QString storagePath READ storagePath CONSTANT)
    Q_PROPERTY(QString mediaPath READ mediaPath CONSTANT)
    Q_PROPERTY(QString statusMessage READ statusMessage NOTIFY statusMessageChanged)
    Q_PROPERTY(QVariantMap currentCard READ currentCard NOTIFY currentCardChanged)
    Q_PROPERTY(QVariantMap lastError READ lastError NOTIFY lastErrorChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(bool reviewing READ reviewing NOTIFY reviewingChanged)
    Q_PROPERTY(bool reviewingCompletedCard READ reviewingCompletedCard NOTIFY reviewStateChanged)
    Q_PROPERTY(QString reviewCursorVariantId READ reviewCursorVariantId NOTIFY reviewStateChanged)
    Q_PROPERTY(bool answerRevealed READ answerRevealed NOTIFY answerRevealedChanged)
    Q_PROPERTY(bool paused READ paused NOTIFY pausedChanged)
    Q_PROPERTY(int queueCount READ queueCount NOTIFY reviewStateChanged)
    Q_PROPERTY(int reviewedCount READ reviewedCount NOTIFY reviewStateChanged)
    Q_PROPERTY(int sessionTotal READ sessionTotal NOTIFY reviewStateChanged)
    Q_PROPERTY(double responseSeconds READ responseSeconds NOTIFY responseSecondsChanged)
    Q_PROPERTY(QString spokenAnswer READ spokenAnswer NOTIFY spokenAnswerChanged)
    Q_PROPERTY(QVariantList pendingCards READ pendingCards NOTIFY reviewStateChanged)
    Q_PROPERTY(QVariantList reviewCards READ reviewCards NOTIFY reviewStateChanged)
    Q_PROPERTY(QVariantMap pendingGradeCorrection READ pendingGradeCorrection NOTIFY pendingGradeCorrectionChanged)
public:
    explicit AppController(const QString &storageDir, QObject *parent = nullptr);
    ~AppController() override;

    QVariantList decks() const { return m_decks; }
    QVariantList cards() const { return m_cards; }
    QVariantList history() const { return m_history; }
    QString selectedDeckId() const { return m_selectedDeckId; }
    QString storagePath() const { return m_storagePath; }
    QString mediaPath() const;
    QString statusMessage() const { return m_statusMessage; }
    QVariantMap currentCard() const { return m_currentCard; }
    QVariantMap lastError() const { return m_lastError; }
    bool busy() const { return m_busy; }
    bool reviewing() const { return m_reviewing; }
    bool reviewingCompletedCard() const { return m_reviewingCompletedCard; }
    QString reviewCursorVariantId() const { return m_reviewCursorVariantId; }
    bool answerRevealed() const { return m_answerRevealed; }
    bool paused() const { return m_paused; }
    int queueCount() const { return m_queueCount; }
    int reviewedCount() const { return m_reviewedCount; }
    int sessionTotal() const { return m_sessionTotal; }
    double responseSeconds() const { return m_responseSeconds; }
    QString spokenAnswer() const { return m_spokenAnswer; }
    QVariantList pendingCards() const { return m_pendingCards; }
    QVariantList reviewCards() const { return m_reviewCards; }
    QVariantMap pendingGradeCorrection() const { return m_pendingGradeCorrection; }

    Q_INVOKABLE void setSelectedDeckId(const QString &id);
    Q_INVOKABLE void createDeck(const QString &name, const QString &description = {}, const QString &parentId = {});
    Q_INVOKABLE void updateDeck(const QString &id, const QString &name, const QString &description = {}, const QString &parentId = {});
    Q_INVOKABLE void deleteDeck(const QString &id);
    Q_INVOKABLE bool saveCard(const QString &id, const QString &deckId, const QString &kind,
                              const QString &front, const QString &back, const QString &tags,
                              int pointCount = 1);
    Q_INVOKABLE void deleteCard(const QString &id);
    Q_INVOKABLE bool replaceCardWithAtomicCards(const QVariantMap &expectedSource,const QVariantList &proposals);
    Q_INVOKABLE void startReview(const QString &deckId = {});
    Q_INVOKABLE void selectReviewCard(const QString &variantId);
    Q_INVOKABLE void navigateReview(int offset);
    Q_INVOKABLE void revealAnswer();
    Q_INVOKABLE void grade(int grade, double recallFraction = -1.0);
    Q_INVOKABLE void confirmGradeCorrection();
    Q_INVOKABLE void cancelGradeCorrection();
    Q_INVOKABLE void deferCard();
    Q_INVOKABLE void postponeCard(const QString &date);
    Q_INVOKABLE void postponeDays(int days);
    Q_INVOKABLE void pauseReview();
    Q_INVOKABLE void resumeReview();
    Q_INVOKABLE void stopReview();
    Q_INVOKABLE void setSpokenAnswer(const QString &answer);
    Q_INVOKABLE void refresh();
    Q_INVOKABLE void exportCollection(const QString &path);
    Q_INVOKABLE void importCollection(const QString &path);
    Q_INVOKABLE void loadExampleDeck();
    Q_INVOKABLE void requestSyncBatch(const QString &requestId = {});
    Q_INVOKABLE void applySyncResponse(const QVariantMap &response);

signals:
    void decksChanged(); void cardsChanged(); void historyChanged();
    void selectedDeckIdChanged(); void statusMessageChanged(); void currentCardChanged();
    void lastErrorChanged(); void busyChanged(); void reviewingChanged();
    void answerRevealedChanged(); void pausedChanged(); void reviewStateChanged();
    void responseSecondsChanged(); void spokenAnswerChanged();
    void pendingGradeCorrectionChanged();
    void atomicSplitApplied(const QString &sourceId,bool success,const QStringList &newIds);
    void syncBatchReady(const QVariantMap &batch);
    void syncBatchFailed(const QString &code, const QString &message);
    void syncBatchFailedForRequest(const QString &requestId,const QString &code,const QString &message);
    void syncResponseApplied(const QString &requestId,bool success,bool hasMore);
    void syncApplied(bool success, bool hasMore);

private:
    void invoke(const std::function<void(DatabaseWorker *)> &call);
    void handleSnapshot(const QVariantList &decks, const QVariantList &cards, const QVariantList &history);
    void handleOperation(bool ok, const QString &message, const QVariantMap &error);
    void handleQueue(const QVariantList &cards, int total, bool resetCurrent, const QVariantMap &selectedCard,
                     bool inspecting, const QString &cursorVariantId);
    void setBusy(bool value);
    void setError(const QVariantMap &error);
    void updateElapsed();
    void freezeElapsed();
    void resetElapsed();
    bool reviewActionReady(bool needsAnswer);

    DatabaseWorker *m_worker = nullptr;
    QThread *m_thread = nullptr;
    QString m_storagePath;
    QString m_selectedDeckId;
    QString m_statusMessage;
    QString m_spokenAnswer;
    QString m_reviewCursorVariantId;
    QVariantList m_decks,m_cards,m_history,m_pendingCards,m_reviewCards;
    QVariantMap m_currentCard,m_lastError,m_pendingGradeCorrection;
    QSet<QString> m_leftReviewedVariants;
    bool m_busy = true;
    bool m_reviewing = false;
    bool m_reviewingCompletedCard = false;
    bool m_answerRevealed = false;
    bool m_paused = false;
    int m_queueCount = 0;
    int m_reviewedCount = 0;
    int m_sessionTotal = 0;
    int m_pendingOperations = 1;
    double m_responseSeconds = 0.0;
    double m_accumulatedSeconds = 0.0;
    QElapsedTimer m_elapsed;
    QTimer m_elapsedTick;
    void clearPendingGradeCorrection();
};
