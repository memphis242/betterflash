#include "appcontroller.h"
#include "databaseworker.h"

#include <QDate>
#include <QDir>
#include <QFileInfo>
#include <QMetaObject>
#include <QThread>
#include <algorithm>
#include <cassert>
#include <cmath>

AppController::AppController(const QString &storageDir,QObject *parent)
    : QObject(parent),m_worker(new DatabaseWorker(QDir(storageDir).filePath(QStringLiteral("betterflash.sqlite")))),
      m_thread(new QThread(this)),m_storagePath(QDir(storageDir).filePath(QStringLiteral("betterflash.sqlite")))
{
    m_worker->moveToThread(m_thread);
    connect(m_thread,&QThread::started,m_worker,&DatabaseWorker::initialize);
    connect(m_worker,&DatabaseWorker::snapshotReady,this,&AppController::handleSnapshot);
    connect(m_worker,&DatabaseWorker::operationFinished,this,&AppController::handleOperation);
    connect(m_worker,&DatabaseWorker::reviewQueueReady,this,&AppController::handleQueue);

    connect(m_worker,&DatabaseWorker::atomicSplitApplied,this,&AppController::atomicSplitApplied);
    connect(m_worker,&DatabaseWorker::syncBatchReady,this,&AppController::syncBatchReady);
    connect(m_worker,&DatabaseWorker::syncBatchFailed,this,&AppController::syncBatchFailed);
    connect(m_worker,&DatabaseWorker::syncBatchFailedForRequest,this,&AppController::syncBatchFailedForRequest);
    connect(m_worker,&DatabaseWorker::syncResponseApplied,this,&AppController::syncResponseApplied);
    connect(m_worker,&DatabaseWorker::syncApplied,this,&AppController::syncApplied);
    connect(m_worker,&DatabaseWorker::cardTimelineReady,this,&AppController::cardTimelineReady);
    connect(m_worker,&DatabaseWorker::studyAnalyticsReady,this,&AppController::studyAnalyticsReady);
    connect(m_worker,&DatabaseWorker::moveCardsFinished,this,&AppController::moveCardsFinished);
    connect(m_worker,&DatabaseWorker::moveDeckFinished,this,&AppController::moveDeckFinished);
    connect(m_worker,&DatabaseWorker::cardSaved,this,&AppController::cardSaved);
    connect(m_worker,&DatabaseWorker::cardNarrationSaved,this,&AppController::cardNarrationSaved);
    connect(m_thread,&QThread::finished,m_worker,&QObject::deleteLater);
    m_elapsedTick.setInterval(250);
    connect(&m_elapsedTick,&QTimer::timeout,this,&AppController::updateElapsed);
    m_thread->start();
}
AppController::~AppController()
{
    m_elapsedTick.stop();
    // SQLite connections are closed and removed by the thread that owns them.
    QMetaObject::invokeMethod(m_worker,[worker=m_worker]{worker->shutdown();},Qt::BlockingQueuedConnection);
    m_thread->quit();m_thread->wait();
}
QString AppController::mediaPath() const { return QDir(QFileInfo(m_storagePath).absolutePath()).filePath(QStringLiteral("media")); }
bool AppController::invoke(const std::function<void(DatabaseWorker *)> &call)
{
    assert(thread()==QThread::currentThread());
    ++m_pendingOperations;setBusy(true);
    const bool queued=QMetaObject::invokeMethod(m_worker,[worker=m_worker,call]{call(worker);},Qt::QueuedConnection);
    if (!queued) {
        handleOperation(false,"Could not queue the operation. Restart the application.",{{"code","WORKER_UNAVAILABLE"},{"message","Could not queue the operation. Restart the application."},{"detail",QString()}});
        return false;
    }
    return true;
}
void AppController::setBusy(bool value) { if (m_busy!=value) {m_busy=value;emit busyChanged();} }
void AppController::setError(const QVariantMap &error) {m_lastError=error;emit lastErrorChanged();}
void AppController::setSelectedDeckId(const QString &id)
{ if (m_selectedDeckId!=id) {m_selectedDeckId=id;emit selectedDeckIdChanged();} }
void AppController::createDeck(const QString &name,const QString &description,const QString &parentId)
{ invoke([=](DatabaseWorker *worker){worker->createDeck(name,description,parentId);}); }
void AppController::updateDeck(const QString &id,const QString &name,const QString &description,const QString &parentId)
{ invoke([=](DatabaseWorker *worker){worker->updateDeck(id,name,description,parentId);}); }
bool AppController::moveDeck(const QString &id, const QString &parentId)
{
    if (m_busy) {
        setError({{"code", "OPERATION_BUSY"}, {"message", "Finish the current operation before moving a deck."}, {"detail", QString()}});
        emit moveDeckFinished(false, id, parentId);
        return false;
    }
    if (!betterflash::model::validUuid(id) || (!parentId.isEmpty() && !betterflash::model::validUuid(parentId))) {
        setError({{"code", "INVALID_DECK_MOVE"}, {"message", "Choose a valid deck and destination."}, {"detail", QString()}});
        emit moveDeckFinished(false, id, parentId);
        return false;
    }
    const bool queued = invoke([=](DatabaseWorker *worker) { worker->moveDeck(id, parentId); });
    if (!queued) emit moveDeckFinished(false, id, parentId);
    return queued;
}
void AppController::deleteDeck(const QString &id)
{ invoke([=](DatabaseWorker *worker){worker->deleteDeck(id);}); }
bool AppController::saveCard(const QString &id,const QString &deckId,const QString &kind,const QString &front,const QString &back,const QString &tags,int points,const QString &requestId)
{
    const struct betterflash::model::Card candidate{id.isEmpty()?betterflash::model::uuid():id,deckId,kind,front,back,tags,points};
    const auto valid=betterflash::model::cardFromMap(betterflash::model::toMap(candidate));
    if (!valid) {
        setError({{"code",kind==QStringLiteral("cloze")?"INVALID_CLOZE":"INVALID_CARD"},{"message","Could not save the card. Correct the card fields."},{"detail",valid.error()}});
        emit cardSaved(id,requestId,false);
        return false;
    }
    const bool queued = invoke([=](DatabaseWorker *worker){worker->saveCard(id,deckId,kind,front,back,tags,points,requestId);});
    if (!queued) emit cardSaved(id,requestId,false);
    return queued;
}
bool AppController::setCardNarration(const QString &cardId,const QString &variantKey,const QString &side,const QString &mediaName,const QString &requestId,const QString &fingerprint)
{
    if (m_busy) {
        setError({{"code","OPERATION_BUSY"},{"message","Finish the current operation before saving narration."},{"detail",QString()}});
        emit cardNarrationSaved(cardId,variantKey,side,requestId,false);
        return false;
    }
    if (!betterflash::model::validUuid(cardId) || variantKey.isEmpty()
        || (side != QStringLiteral("front") && side != QStringLiteral("back"))) {
        setError({{"code","NARRATION_REQUEST_INVALID"},{"message","Choose a valid card narration target."},{"detail",QString()}});
        emit cardNarrationSaved(cardId,variantKey,side,requestId,false);
        return false;
    }
    const bool queued = invoke([=](DatabaseWorker *worker) {
        worker->setCardNarration(cardId,variantKey,side,mediaName,requestId,fingerprint);
    });
    if (!queued) emit cardNarrationSaved(cardId,variantKey,side,requestId,false);
    return queued;
}
bool AppController::confirmCardNarration(const QString &cardId,const QString &variantKey,const QString &side,const QString &mediaName,const QString &expectedFingerprint,const QString &requestId)
{
    if (m_busy) {
        setError({{"code","OPERATION_BUSY"},{"message","Finish the current operation before confirming narration."},{"detail",QString()}});
        emit cardNarrationSaved(cardId,variantKey,side,requestId,false);
        return false;
    }
    if (!betterflash::model::validUuid(cardId) || variantKey.isEmpty()
        || (side != QStringLiteral("front") && side != QStringLiteral("back"))) {
        setError({{"code","NARRATION_REQUEST_INVALID"},{"message","Choose a valid card narration target."},{"detail",QString()}});
        emit cardNarrationSaved(cardId,variantKey,side,requestId,false);
        return false;
    }
    const bool queued = invoke([=](DatabaseWorker *worker) {
        worker->confirmCardNarration(cardId,variantKey,side,mediaName,expectedFingerprint,requestId);
    });
    if (!queued) emit cardNarrationSaved(cardId,variantKey,side,requestId,false);
    return queued;
}
void AppController::deleteCard(const QString &id)
{ invoke([=](DatabaseWorker *worker){worker->deleteCard(id);}); }
bool AppController::moveCards(const QVariantList &cardIds, const QString &destinationDeckId, const QString &requestId)
{
    if (m_busy) {
        setError({{"code", "OPERATION_BUSY"}, {"message", "Finish the current operation before moving cards."}, {"detail", QString()}});
        emit moveCardsFinished(false, destinationDeckId, requestId);
        return false;
    }
    if (cardIds.isEmpty() || cardIds.size() > betterflash::model::maxCollectionRecords
        || !betterflash::model::validUuid(destinationDeckId)) {
        setError({{"code", "INVALID_CARD_MOVE"}, {"message", "Choose one or more cards and a valid destination deck."}, {"detail", QString()}});
        emit moveCardsFinished(false, destinationDeckId, requestId);
        return false;
    }
    const bool queued = invoke([=](DatabaseWorker *worker) { worker->moveCards(cardIds, destinationDeckId, requestId); });
    if (!queued) emit moveCardsFinished(false, destinationDeckId, requestId);
    return queued;
}
bool AppController::developmentToolsEnabled() const
{
#ifdef BETTERFLASH_DEVELOPMENT_TOOLS
    return true;
#else
    return false;
#endif
}
void AppController::resetDeck(const QString &id)
{
    if (m_busy) return;
    invoke([=](DatabaseWorker *worker){worker->resetReviewState(id,true);});
}
void AppController::resetCard(const QString &id)
{
    if (m_busy) return;
    invoke([=](DatabaseWorker *worker){worker->resetReviewState(id,false);});
}
bool AppController::replaceCardWithAtomicCards(const QVariantMap &expectedSource,const QVariantList &proposals)
{
    const auto source=betterflash::model::cardFromMap(expectedSource);
    if (!source) {
        setError({{"code","INVALID_ATOMIC_SPLIT"},{"message","Could not replace the card. Choose a valid source card."},{"detail",source.error()}});return false;
    }
    const auto valid=betterflash::model::atomicCardsFromProposals(*source,proposals);
    if (!valid) {
        setError({{"code","INVALID_ATOMIC_SPLIT"},{"message","Could not replace the card. Correct the proposed cards."},{"detail",valid.error()}});return false;
    }
    invoke([=](DatabaseWorker *worker){worker->replaceCardWithAtomicCards(expectedSource,proposals);});return true;
}
void AppController::startReview(const QString &deckId)
{
    if (m_busy) return;
    m_reviewedCount=0;m_sessionTotal=0;m_spokenAnswer.clear();m_paused=false;m_leftReviewedVariants.clear();clearPendingGradeCorrection();
    emit spokenAnswerChanged();emit pausedChanged();emit reviewStateChanged();
    invoke([=](DatabaseWorker *worker){worker->beginReview(deckId);});
}
void AppController::selectReviewCard(const QString &variantId)
{
    if (m_busy||m_paused) return;
    if (!m_reviewing) {
        setError({{"code","REVIEW_INACTIVE"},{"message","Start a review before choosing a card."},{"detail",QString()}});return;
    }
    if (variantId==m_currentCard.value("variantId").toString()) return;
    clearPendingGradeCorrection();
    invoke([=](DatabaseWorker *worker){worker->selectReviewCard(variantId);});
}
void AppController::navigateReview(int offset)
{
    if (m_busy||m_paused||!m_reviewing||offset==0||m_reviewCards.isEmpty()) return;
    const QString current=m_currentCard.value("variantId").toString();
    const auto found=std::find_if(m_reviewCards.cbegin(),m_reviewCards.cend(),[&](const QVariant &value){return value.toMap().value("variantId").toString()==current;});
    if (found==m_reviewCards.cend()) return;
    const qint64 index=found-m_reviewCards.cbegin();
    const qint64 target=std::clamp(index+static_cast<qint64>(offset),qint64(0),static_cast<qint64>(m_reviewCards.size()-1));
    if (target!=index) selectReviewCard(m_reviewCards.at(target).toMap().value("variantId").toString());
}
void AppController::updateElapsed()
{
    if (m_elapsed.isValid()) {
        m_responseSeconds=std::min(86400.0,m_accumulatedSeconds+m_elapsed.nsecsElapsed()/1.0e9);
        emit responseSecondsChanged();
    }
}
void AppController::freezeElapsed()
{
    updateElapsed();m_accumulatedSeconds=m_responseSeconds;m_elapsed.invalidate();m_elapsedTick.stop();
}
void AppController::resetElapsed()
{
    m_elapsed.invalidate();m_accumulatedSeconds=0;m_responseSeconds=0;
    if (m_reviewing&&!m_paused&&!m_reviewingCompletedCard&&m_currentCard.value("sessionGrade",-1).toInt()<0) {m_elapsed.start();m_elapsedTick.start();}
    else m_elapsedTick.stop();
    emit responseSecondsChanged();
}
bool AppController::reviewActionReady(bool needsAnswer, bool allowPaused)
{
    if (m_busy||(m_paused&&!allowPaused)) return false;
    if (!m_reviewing||m_currentCard.isEmpty()||(needsAnswer&&!m_answerRevealed)) {
        setError({{"code","REVIEW_NOT_READY"},{"message",needsAnswer?"Reveal the answer before grading this card.":"Start a review first."},{"detail",QString()}});return false;
    }
    return true;
}
void AppController::revealAnswer()
{
    if (!reviewActionReady(false)||m_answerRevealed) return;
    freezeElapsed();m_answerRevealed=true;emit answerRevealedChanged();
}
void AppController::grade(int grade,double recall)
{
    if (grade<0||grade>4||!std::isfinite(recall)||(recall!=-1.0&&(recall<0||recall>1))) {
        setError({{"code","INVALID_GRADE"},{"message","Choose a grade from 0 to 4 and a recall fraction between 0 and 1."},{"detail",QString()}});return;
    }
    if (!reviewActionReady(true)) return;
    const double fraction=recall==-1.0?(grade==0?0.0:grade==1?0.5:1.0):recall;
    const int existing=m_currentCard.value("sessionGrade",-1).toInt();
    const double currentRecall=m_currentCard.value("sessionRecall",-1.0).toDouble();
    if (existing>=0&&m_leftReviewedVariants.contains(m_currentCard.value("variantId").toString())
        &&(existing!=grade||!qFuzzyCompare(currentRecall+1.0,fraction+1.0))) {
        m_pendingGradeCorrection={{"variantId",m_currentCard.value("variantId")},{"card",m_currentCard},{"currentGrade",existing},{"currentRecall",m_currentCard.value("sessionRecall",-1.0)},{"requestedGrade",grade},{"requestedRecall",fraction}};
        emit pendingGradeCorrectionChanged();
        return;
    }
    clearPendingGradeCorrection();
    const double seconds=m_responseSeconds;
    const QString expectedVariant=m_currentCard.value("variantId").toString();
    invoke([=](DatabaseWorker *worker){worker->gradeCard(grade,fraction,seconds,expectedVariant);});
}
void AppController::confirmGradeCorrection()
{
    if (m_pendingGradeCorrection.isEmpty()||m_busy||m_paused||!m_reviewing||!m_answerRevealed) return;
    const QVariantMap pending=m_pendingGradeCorrection;
    if (pending.value("variantId").toString()!=m_currentCard.value("variantId").toString()
        ||pending.value("card").toMap().value("sessionReviewId")!=m_currentCard.value("sessionReviewId")
        ||pending.value("currentGrade").toInt()!=m_currentCard.value("sessionGrade",-1).toInt()
        ||!qFuzzyCompare(pending.value("currentRecall").toDouble()+1.0,m_currentCard.value("sessionRecall",-1.0).toDouble()+1.0)) { clearPendingGradeCorrection(); return; }
    clearPendingGradeCorrection();
    const QString expected=pending.value("variantId").toString();
    invoke([=](DatabaseWorker *worker){worker->gradeCard(pending.value("requestedGrade").toInt(),pending.value("requestedRecall").toDouble(),pending.value("card").toMap().value("responseSeconds",0.0).toDouble(),expected);});
}
void AppController::cancelGradeCorrection()
{ clearPendingGradeCorrection(); }
void AppController::clearPendingGradeCorrection()
{
    if (m_pendingGradeCorrection.isEmpty()) return;
    m_pendingGradeCorrection.clear();emit pendingGradeCorrectionChanged();
}
void AppController::deferCard()
{
    if (reviewActionReady(false, true)) {clearPendingGradeCorrection();invoke([](DatabaseWorker *worker){worker->defer();});}
}
void AppController::postponeCard(const QString &date)
{
    if (reviewActionReady(false, true)) {clearPendingGradeCorrection();invoke([=](DatabaseWorker *worker){worker->postpone(date);});}
}
void AppController::postponeDays(int days)
{
    if (days<1||days>36500) {setError({{"code","INVALID_POSTPONE_DATE"},{"message","Choose between 1 and 36500 days."},{"detail",QString()}});return;}
    postponeCard(QDate::currentDate().addDays(days).toString(Qt::ISODate));
}
void AppController::pauseReview()
{
    if (!m_reviewing||m_paused) return;
    clearPendingGradeCorrection();freezeElapsed();m_paused=true;emit pausedChanged();
}
void AppController::resumeReview()
{
    if (!m_reviewing||!m_paused) return;
    m_paused=false;
    if (!m_answerRevealed&&!m_reviewingCompletedCard&&m_currentCard.value("sessionGrade",-1).toInt()<0) {m_elapsed.start();m_elapsedTick.start();}
    emit pausedChanged();
}
void AppController::stopReview()
{
    if (!m_reviewing) return;
    clearPendingGradeCorrection();freezeElapsed();invoke([](DatabaseWorker *worker){worker->endReview();});
}
void AppController::setSpokenAnswer(const QString &answer)
{m_spokenAnswer=answer.left(1024*1024);emit spokenAnswerChanged();}
void AppController::refresh() {invoke([](DatabaseWorker *worker){worker->snapshot();});}
void AppController::requestSyncBatch(const QString &requestId) {invoke([=](DatabaseWorker *worker){worker->requestSyncBatch(requestId);});}
void AppController::applySyncResponse(const QVariantMap &response)
{invoke([=](DatabaseWorker *worker){worker->applySyncResponse(response);});}
void AppController::exportCollection(const QString &path)
{invoke([=](DatabaseWorker *worker){worker->exportCollection(path);});}
void AppController::importCollection(const QString &path)
{invoke([=](DatabaseWorker *worker){worker->importCollection(path);});}
void AppController::loadExampleDeck() {invoke([](DatabaseWorker *worker){worker->loadExample();});}
void AppController::loadCardTimeline(const QString &cardId, int offset, const QString &requestId)
{
    const QString requestedId=cardId;
    const int requestedOffset=offset;
    const QString requestedRequestId=requestId;
    const bool queued=QMetaObject::invokeMethod(m_worker,[worker=m_worker,requestedId,requestedOffset,requestedRequestId]{
        worker->loadCardTimeline(requestedId,requestedOffset,requestedRequestId);
    },Qt::QueuedConnection);
    if (!queued) emit cardTimelineReady(requestedId,{{"requestId",requestedRequestId},{"offset",requestedOffset},{"events",QVariantList{}},{"totalEvents",0},{"hasMore",false},{"nextOffset",0},{"reviewCount",0},{"successfulCount",0},{"averageRecall",0.0},{"averageSeconds",0.0},{"firstRecordedAt",QString()},{"firstSavedAt",QString()},{"error",QVariantMap{{"code","WORKER_UNAVAILABLE"},{"message","Could not load card history. Restart the application."},{"details",QString()}}}});
}
void AppController::loadStudyAnalytics(const QStringList &deckIds, const QString &requestId)
{
    const QStringList requestedDeckIds = deckIds;
    const QString requestedRequestId = requestId;
    const QString requestedToday = QDate::currentDate().toString(Qt::ISODate);
    const bool queued = QMetaObject::invokeMethod(m_worker, [worker = m_worker, requestedDeckIds, requestedRequestId] {
        worker->loadStudyAnalytics(requestedDeckIds, requestedRequestId);
    }, Qt::QueuedConnection);
    if (!queued) {
        emit studyAnalyticsReady(requestedDeckIds, {{"requestId", requestedRequestId}, {"deckIds", QVariant::fromValue(requestedDeckIds)},
            {"today", requestedToday}, {"firstDate", requestedToday},
            {"days", QVariantList{}}, {"stats", QVariantMap{{"practiceScore", QVariant{}}, {"averageRecall", QVariant{}}, {"averageGrade", QVariant{}},
                {"eligibleDecks", 0}, {"reviewedDecks", 0}, {"totalVariants", 0}, {"reviewedVariants", 0}, {"activeDays", 0},
                {"windowDays", 30}, {"recallChange", QVariant{}}, {"currentReviewCount", 0}}},
            {"error", QVariantMap{{"code", "WORKER_UNAVAILABLE"}, {"message", "Could not load study analytics. Restart the application."}, {"detail", QString()}}}});
    }
}
void AppController::handleSnapshot(const QVariantList &decks,const QVariantList &cards,const QVariantList &history,const QVariantList &reviewSessions)
{
    const bool sessionsChanged=m_reviewSessions!=reviewSessions;
    m_decks=decks;m_cards=cards;m_history=history;m_reviewSessions=reviewSessions;
    emit decksChanged();emit cardsChanged();emit historyChanged();if (sessionsChanged) emit reviewSessionsChanged();
    if (!m_selectedDeckId.isEmpty()) {
        const bool exists=std::any_of(m_decks.begin(),m_decks.end(),[&](const QVariant &d){return d.toMap().value("id").toString()==m_selectedDeckId;});
        if (!exists) setSelectedDeckId({});
    }
}
void AppController::handleOperation(bool ok,const QString &message,const QVariantMap &error)
{
    assert(m_pendingOperations>0);--m_pendingOperations;setBusy(m_pendingOperations>0);
    if (!message.isEmpty()) {m_statusMessage=message;emit statusMessageChanged();}
    if (!ok) setError(error);
    else if (!m_lastError.isEmpty()) {m_lastError.clear();emit lastErrorChanged();}
}
void AppController::handleQueue(const QVariantList &cards,int total,bool resetCurrent,const QVariantMap &selectedCard,
                                bool inspecting,const QString &cursorVariantId)
{
    const bool moved=m_currentCard.value("variantId")!=selectedCard.value("variantId");
    const bool contentChanged=m_currentCard.value("front")!=selectedCard.value("front")||m_currentCard.value("back")!=selectedCard.value("back");
    const bool changed=resetCurrent||moved||contentChanged;
    if (moved) {
        const QString previous=m_currentCard.value("variantId").toString();
        const bool graded=m_currentCard.value("sessionGrade",-1).toInt()>=0||std::any_of(cards.cbegin(),cards.cend(),[&](const QVariant &value){
            const QVariantMap card=value.toMap();return card.value("variantId").toString()==previous&&card.value("sessionGrade",-1).toInt()>=0;
        });
        if (graded) m_leftReviewedVariants.insert(previous);
    }
    if (changed||m_currentCard.value("sessionReviewId")!=selectedCard.value("sessionReviewId")
        ||m_currentCard.value("sessionGrade")!=selectedCard.value("sessionGrade")||m_currentCard.value("sessionRecall")!=selectedCard.value("sessionRecall")) clearPendingGradeCorrection();
    const bool wasCompleted=m_reviewingCompletedCard;
    m_reviewCards=cards;m_pendingCards.clear();
    for (const QVariant &value : cards) if (value.toMap().value("sessionGrade",-1).toInt()<0) m_pendingCards.append(value);
    m_queueCount=m_pendingCards.size();m_sessionTotal=total;m_currentCard=selectedCard;
    if (!cards.isEmpty()||total==0) m_reviewedCount=cards.size()-m_pendingCards.size();
    m_reviewingCompletedCard=inspecting;m_reviewCursorVariantId=cursorVariantId;
    const bool reviewing=!cards.isEmpty();
    if (reviewing!=m_reviewing) {m_reviewing=reviewing;emit reviewingChanged();}
    if (changed) {
        m_answerRevealed=inspecting;m_spokenAnswer.clear();
        if (!m_reviewing&&m_paused) {m_paused=false;emit pausedChanged();}
        resetElapsed();
        emit answerRevealedChanged();emit spokenAnswerChanged();
    }
    if (inspecting) {
        m_elapsed.invalidate();m_elapsedTick.stop();
        m_accumulatedSeconds=selectedCard.value("responseSeconds",0.0).toDouble();m_responseSeconds=m_accumulatedSeconds;
        emit responseSecondsChanged();
        if (!m_answerRevealed) {m_answerRevealed=true;emit answerRevealedChanged();}
    } else if (wasCompleted&&!changed) resetElapsed();
    emit currentCardChanged();emit reviewStateChanged();
}
