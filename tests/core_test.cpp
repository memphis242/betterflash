#include "core/appcontroller.h"
#include "core/cardmodel.h"
#include "core/scheduler.h"
#include "core/mediabackup.h"
#include "core/syncprotocol.h"

#include <QDate>
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSignalSpy>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTest>
#include <QTimeZone>
#include <cmath>
#include <limits>

namespace model = betterflash::model;
namespace {
bool idle(AppController &app)
{
    QElapsedTimer timer;timer.start();
    while (app.busy()&&timer.elapsed()<5000) QTest::qWait(10);
    return !app.busy();
}
QVariantMap batch(AppController &app,const QString &requestId={})
{
    QSignalSpy spy(&app,&AppController::syncBatchReady);
    app.requestSyncBatch(requestId);
    if (spy.isEmpty()&&!spy.wait(5000)) return {};
    if (!idle(app)) return {};
    return spy.first().at(0).toMap();
}
bool apply(AppController &app,const QVariantMap &response)
{
    QSignalSpy spy(&app,&AppController::syncApplied);app.applySyncResponse(response);
    if (spy.isEmpty()&&!spy.wait(5000)) return false;
    return idle(app)&&spy.first().at(0).toBool();
}
QString addDeck(AppController &app,const QString &name=QStringLiteral("Deck"))
{
    app.createDeck(name);if (!idle(app)||app.decks().isEmpty()) {qWarning()<<app.lastError();return {};}
    return app.decks().last().toMap().value("id").toString();
}
QString addCard(AppController &app,const QString &deck,const QString &kind,const QString &front,const QString &back,int points=1)
{
    QSet<QString> existing;for (const QVariant &v:app.cards()) existing.insert(v.toMap().value("id").toString());
    app.saveCard({},deck,kind,front,back,{},points);if (!idle(app)) return {};
    if (!app.lastError().isEmpty()) qWarning()<<app.lastError();
    for (const QVariant &v:app.cards()) if (!existing.contains(v.toMap().value("id").toString())) return v.toMap().value("id").toString();
    return {};
}
QVariantMap card(AppController &app,const QString &id)
{
    for (const QVariant &v:app.cards()) if (v.toMap().value("id").toString()==id) return v.toMap();
    return {};
}
QVariantMap variant(const QVariantMap &card,const QString &key)
{
    for (const QVariant &v:card.value("variants").toList()) if (v.toMap().value("key").toString()==key) return v.toMap();
    return {};
}
struct DatabaseAccess {
    const QString name=model::uuid();
    QSqlDatabase db=QSqlDatabase::addDatabase("QSQLITE",name);
    explicit DatabaseAccess(const QString &path) {db.setDatabaseName(path);if (!db.open()) qFatal("Test storage access failed");}
    ~DatabaseAccess() {db.close();db=QSqlDatabase();QSqlDatabase::removeDatabase(name);}
    bool run(const QString &sql) {QSqlQuery q(db);return q.exec(sql);}
    int count(const QString &table) {QSqlQuery q(db);if (!q.exec("SELECT COUNT(*) FROM "+table)||!q.next()) return -1;return q.value(0).toInt();}
};
QJsonObject readJson(const QString &path)
{QFile file(path);if (!file.open(QIODevice::ReadOnly)) return {};return QJsonDocument::fromJson(file.readAll()).object();}
bool writeJson(const QString &path,const QJsonObject &object)
{QFile file(path);if (!file.open(QIODevice::WriteOnly)) return false;const QByteArray bytes=QJsonDocument(object).toJson();return file.write(bytes)==bytes.size();}
QByteArray tinyPng()
{return QByteArray::fromHex("89504e470d0a1a0a0000000d4948445200000001000000010804000000b51c0c020000000b4944415478da6364f80f00010501012718e3660000000049454e44ae426082");}

struct FakeServer {
    QVariantList log;
    QSet<QString> ids;
    void append(const QVariantMap &event)
    {
        if (ids.contains(event.value("id").toString())) return;
        ids.insert(event.value("id").toString());
        log.append(QVariantMap{{"seq",static_cast<qint64>(log.size()+1)},{"event",event}});
    }
    QVariantMap exchange(const QVariantMap &request,int limit=100)
    {
        QVariantList accepted;
        for (const QVariant &v:request.value("events").toList()) {append(v.toMap());accepted.append(v.toMap().value("id"));}
        const qint64 since=request.value("cursor").toLongLong();
        QVariantList events;
        for (const QVariant &v:log) {
            if (v.toMap().value("seq").toLongLong()>since&&events.size()<limit) events.append(v);
        }
        const qint64 cursor=events.isEmpty()?since:events.last().toMap().value("seq").toLongLong();
        return {{"acceptedIds",accepted},{"events",events},{"cursor",cursor},{"hasMore",cursor<log.size()},{"requestId",request.value("requestId")}};
    }
};
QVariantMap syncEvent(const QString &type,const QVariantMap &payload)
{return {{"id",model::uuid()},{"deviceId",model::uuid()},{"type",type},{"payload",payload},{"createdAt",model::nowUtc()}};}
QVariantMap response(const QVariantList &events,qint64 cursor,const QVariantList &accepted={})
{return {{"acceptedIds",accepted},{"events",events},{"cursor",cursor},{"hasMore",false}};}
}

class CoreTest final : public QObject {
    Q_OBJECT
private slots:
    void schedulerGradesFractionsLatencyAndBounds();
    void reverseVariantsAreIndependent();
    void clozeGroupsRenderAndScheduleIndependently();
    void clozeEscapesPreserveScopesAndMultilineText();
    void malformedClozeIsRejected_data();
    void malformedClozeIsRejected();
    void queueRotationDoesNotChangeSchedules();
    void postponeSurvivesRestartAndKeepsOtherVariantDue();
    void actualTimingExcludesPauseAndGradingDelay();
    void invalidGradesLeaveReviewUnchanged();
    void mutationsAndOutboxRollBackTogether();
    void staleQueueEntriesAreRemoved();
    void exportImportAndValidation();
    void importRollsBackOnStorageFailure();
    void retainedDeletedHistoryRoundTrips();
    void mediaBackupsRoundTripAndRejectUnsafeData();
    void preserveDamagedMediaUsesDirectoryFd();
    void syncRoundTripIdempotenceAndDeletion();
    void syncProtectsPendingEditsAcrossDownloadPages();
    void syncMissingDependenciesAreDurable();
    void invalidSyncDoesNotAcknowledgeUploads();
    void syncByteCapsAndFailureSignals();
    void syncRequiresIssuedAcknowledgmentsAndContiguousCursor();
    void syncRequestCorrelationAndTransactionalRollback();
    void syncMediaBatchesSplitBeforeLimits();
    void atomicReplacementIsDurableAndPreservesHistory();
    void atomicReplacementRejectsInvalidStaleAndFailedMutations();
    void examplesAreExplicitAndAtomic();
};

void CoreTest::schedulerGradesFractionsLatencyAndBounds()
{
    double previous=0;
    for (int grade=0;grade<=4;++grade) {
        const struct betterflash::scheduler::ReviewResult result=betterflash::scheduler::schedule(10,.5,grade,1,2,4);
        QVERIFY(result.stability>previous);previous=result.stability;
    }
    const struct betterflash::scheduler::ReviewResult partialLow=betterflash::scheduler::schedule(10,.5,1,.2,2,4);
    const struct betterflash::scheduler::ReviewResult partialHigh=betterflash::scheduler::schedule(10,.5,1,.8,2,4);
    QVERIFY(partialHigh.stability>partialLow.stability);
    QVERIFY(partialHigh.difficulty<partialLow.difficulty);
    const struct betterflash::scheduler::ReviewResult easier=betterflash::scheduler::schedule(10,.1,3,1,2,4);
    const struct betterflash::scheduler::ReviewResult harder=betterflash::scheduler::schedule(10,.9,3,1,2,4);
    QVERIFY(easier.stability>harder.stability);
    QVERIFY(easier.intervalDays>harder.intervalDays);
    const struct betterflash::scheduler::ReviewResult fast=betterflash::scheduler::schedule(10,.5,3,1,2,4);
    const struct betterflash::scheduler::ReviewResult slow=betterflash::scheduler::schedule(10,.5,3,1,90,4);
    QVERIFY(fast.stability>slow.stability);
    const struct betterflash::scheduler::ReviewResult reading=betterflash::scheduler::schedule(10,.5,3,1,90,4,120);
    QCOMPARE(reading.stability,fast.stability);
    for (const double stability:{.25,3650.0}) for (const double difficulty:{0.0,1.0})
        for (int grade=0;grade<=4;++grade) for (const double recall:{0.0,.5,1.0})
            for (const double seconds:{0.0,3600.0}) {
                const struct betterflash::scheduler::ReviewResult r=betterflash::scheduler::schedule(stability,difficulty,grade,recall,seconds,0);
                QVERIFY(std::isfinite(r.stability));QVERIFY(r.stability>=.25&&r.stability<=3650);
                QVERIFY(r.difficulty>=0&&r.difficulty<=1);QVERIFY(r.intervalDays>=1&&r.intervalDays<=3650);
            }
}
void CoreTest::reverseVariantsAreIndependent()
{
    QTemporaryDir directory;AppController app(directory.path());QVERIFY(idle(app));
    const QString deckId=addDeck(app);QVERIFY(!deckId.isEmpty());
    const QString id=addCard(app,deckId,"reverse","Question","Answer");QVERIFY(!id.isEmpty());
    QCOMPARE(card(app,id).value("variants").toList().size(),2);
    app.startReview(deckId);QVERIFY(idle(app));QCOMPARE(app.queueCount(),2);QCOMPARE(app.sessionTotal(),2);
    const QString firstKey=app.currentCard().value("variantKey").toString();
    QCOMPARE(app.currentCard().value("question").toString(),firstKey=="forward"?QStringLiteral("Question"):QStringLiteral("Answer"));
    app.revealAnswer();app.grade(3);QVERIFY(idle(app));
    QCOMPARE(app.reviewedCount(),1);QCOMPARE(app.queueCount(),1);QCOMPARE(app.pendingCards().size(),1);
    const QString otherKey=firstKey=="forward"?"reverse":"forward";
    QCOMPARE(app.currentCard().value("variantKey").toString(),otherKey);
    QCOMPARE(variant(card(app,id),firstKey).value("reviewCount").toInt(),1);
    QCOMPARE(variant(card(app,id),otherKey).value("reviewCount").toInt(),0);
    QCOMPARE(card(app,id).value("front").toString(),QStringLiteral("Question"));
    app.revealAnswer();app.grade(4);QVERIFY(idle(app));
    QCOMPARE(app.reviewedCount(),2);QVERIFY(!app.reviewing());QVERIFY(app.pendingCards().isEmpty());
    QCOMPARE(app.history().size(),2);
    QVERIFY(app.history()[0].toMap().value("variantId")!=app.history()[1].toMap().value("variantId"));
}
void CoreTest::clozeGroupsRenderAndScheduleIndependently()
{
    QTemporaryDir directory;AppController app(directory.path());QVERIFY(idle(app));const QString deckId=addDeck(app);
    const QString source="{{c1::alpha::first}} and {{c2::beta::second}} repeat {{c1::gamma}}.";
    const QString id=addCard(app,deckId,"cloze",source,"Explanation");QVERIFY(!id.isEmpty());
    QCOMPARE(card(app,id).value("variants").toList().size(),2);
    app.startReview(deckId);QVERIFY(idle(app));QCOMPARE(app.currentCard().value("variantKey").toString(),QStringLiteral("c1"));
    const QString question=app.currentCard().value("question").toString();
    QVERIFY(question.contains("[first]"));QVERIFY(question.contains("beta"));QVERIFY(question.contains("[...]"));
    QVERIFY(!question.contains("alpha"));QVERIFY(!question.contains("gamma"));QVERIFY(!question.contains("{{"));
    const QString answer=app.currentCard().value("answer").toString();QVERIFY(answer.contains("alpha"));QVERIFY(answer.contains("gamma"));QVERIFY(answer.contains("Explanation"));
    app.revealAnswer();app.grade(1,.4);QVERIFY(idle(app));
    QCOMPARE(app.currentCard().value("variantKey").toString(),QStringLiteral("c2"));
    QVERIFY(app.currentCard().value("question").toString().contains("alpha"));QVERIFY(app.currentCard().value("question").toString().contains("[second]"));
    QCOMPARE(variant(card(app,id),"c1").value("reviewCount").toInt(),1);QCOMPARE(variant(card(app,id),"c2").value("reviewCount").toInt(),0);
    QCOMPARE(card(app,id).value("front").toString(),source);
}
void CoreTest::clozeEscapesPreserveScopesAndMultilineText()
{
    const struct model::Card note{model::uuid(),model::uuid(),"cloze","{{c1::std\\::expected}} and {{c2::first\nsecond::line one\nline two}}",{}, {},1};
    const auto parts=model::parseCloze(note.front);QVERIFY(parts.has_value());QCOMPARE(parts->size(),2);
    QCOMPARE(parts->at(0).answer,QStringLiteral("std::expected"));QVERIFY(parts->at(0).hint.isEmpty());
    QCOMPARE(parts->at(1).answer,QStringLiteral("first\nsecond"));QCOMPARE(parts->at(1).hint,QStringLiteral("line one\nline two"));
    QVERIFY(model::question(note,"c2").contains("std::expected"));QVERIFY(!model::answer(note,"c2").contains("std\\::expected"));
    const auto standard=model::parseCloze("{{c1::std::expected}}");QVERIFY(standard.has_value());
    QCOMPARE(standard->first().answer,QStringLiteral("std"));QCOMPARE(standard->first().hint,QStringLiteral("expected"));
    for (int original=0;original<=4;++original) {
        const QString encoded="{{c1::literal"+QString(original*2+1,QLatin1Char('\\'))+"::scope}}";
        const auto escaped=model::parseCloze(encoded);QVERIFY(escaped.has_value());
        QCOMPARE(escaped->first().answer,"literal"+QString(original,QLatin1Char('\\'))+"::scope");
    }
}

void CoreTest::malformedClozeIsRejected_data()
{
    QTest::addColumn<QString>("source");
    QTest::newRow("zero group")<<QStringLiteral("{{c0::answer}}");
    QTest::newRow("too large group")<<QStringLiteral("{{c1000::answer}}");
    QTest::newRow("empty answer")<<QStringLiteral("{{c1::}}");
    QTest::newRow("empty hint")<<QStringLiteral("{{c1::answer::}}");
    QTest::newRow("unclosed")<<QStringLiteral("{{c1::answer");
    QTest::newRow("unopened")<<QStringLiteral("answer}}");
    QTest::newRow("nested")<<QStringLiteral("{{c1::{{c2::answer}}}}");
    QTest::newRow("missing marker")<<QStringLiteral("Plain text");
    QTest::newRow("unknown marker")<<QStringLiteral("{{c1::valid}} {{x1::bad}}");
}
void CoreTest::malformedClozeIsRejected()
{
    QFETCH(QString,source);QTemporaryDir directory;AppController app(directory.path());QVERIFY(idle(app));const QString deckId=addDeck(app);
    const QVariantList originalEvents=batch(app).value("events").toList();
    app.saveCard({},deckId,"cloze",source,{},{});QVERIFY(idle(app));QVERIFY(app.cards().isEmpty());
    QCOMPARE(app.lastError().value("code").toString(),QStringLiteral("INVALID_CLOZE"));
    QCOMPARE(batch(app).value("events").toList(),originalEvents);
}
void CoreTest::queueRotationDoesNotChangeSchedules()
{
    QTemporaryDir directory;AppController app(directory.path());QVERIFY(idle(app));const QString deckId=addDeck(app,"First");
    QVERIFY(!addCard(app,deckId,"basic","One","A").isEmpty());QVERIFY(!addCard(app,deckId,"basic","Two","B").isEmpty());
    const QString otherDeck=addDeck(app,"Second");QVERIFY(!addCard(app,otherDeck,"basic","Other deck","C").isEmpty());
    const QVariantList records=app.cards(),events=batch(app).value("events").toList();
    app.startReview(deckId);QVERIFY(idle(app));const QVariantList before=app.pendingCards();QCOMPARE(before.size(),2);
    app.deferCard();QVERIFY(idle(app));const QVariantList after=app.pendingCards();
    QCOMPARE(after.first().toMap().value("variantId"),before.last().toMap().value("variantId"));
    QCOMPARE(after.last().toMap().value("variantId"),before.first().toMap().value("variantId"));
    QCOMPARE(app.currentCard(),after.first().toMap());QCOMPARE(app.reviewedCount(),0);QCOMPARE(app.cards(),records);
    QCOMPARE(batch(app).value("events").toList(),events);
    app.revealAnswer();app.grade(2);QVERIFY(idle(app));
    QCOMPARE(app.pendingCards().size(),1);QCOMPARE(app.pendingCards().first().toMap().value("variantId"),before.first().toMap().value("variantId"));
}
void CoreTest::postponeSurvivesRestartAndKeepsOtherVariantDue()
{
    QTemporaryDir directory;QString id,deckId,postponedKey,deviceId;
    const QDate date=QDate::currentDate().addDays(8);
    const QString utc=date.startOfDay(QTimeZone::systemTimeZone()).toUTC().toString(Qt::ISODateWithMs);
    {
        AppController app(directory.path());QVERIFY(idle(app));deckId=addDeck(app);id=addCard(app,deckId,"reverse","Front","Back");
        app.startReview(deckId);QVERIFY(idle(app));postponedKey=app.currentCard().value("variantKey").toString();
        app.postponeCard(date.toString(Qt::ISODate));QVERIFY(idle(app));
        QCOMPARE(app.queueCount(),1);QCOMPARE(app.reviewedCount(),0);QVERIFY(app.history().isEmpty());
        QCOMPARE(variant(card(app,id),postponedKey).value("due").toString(),utc);
        deviceId=batch(app).value("deviceId").toString();
    }
    {
        AppController app(directory.path());QVERIFY(idle(app));QVERIFY(app.lastError().isEmpty());
        QCOMPARE(batch(app).value("deviceId").toString(),deviceId);
        QCOMPARE(variant(card(app,id),postponedKey).value("due").toString(),utc);
        app.startReview(deckId);QVERIFY(idle(app));QCOMPARE(app.queueCount(),1);
        QVERIFY(app.currentCard().value("variantKey").toString()!=postponedKey);
    }
}
void CoreTest::actualTimingExcludesPauseAndGradingDelay()
{
    QTemporaryDir directory;AppController app(directory.path());QVERIFY(idle(app));const QString deckId=addDeck(app);
    QVERIFY(!addCard(app,deckId,"basic","Question","Answer").isEmpty());app.startReview(deckId);QVERIFY(idle(app));
    QTest::qWait(80);app.pauseReview();const double before=app.responseSeconds();QVERIFY(before>.06);
    QTest::qWait(180);QCOMPARE(app.responseSeconds(),before);app.resumeReview();QTest::qWait(80);app.revealAnswer();
    const double frozen=app.responseSeconds();QVERIFY(frozen>before+.06);QVERIFY(frozen<.35);
    QTest::qWait(180);QCOMPARE(app.responseSeconds(),frozen);app.grade(3);QVERIFY(idle(app));
    QCOMPARE(app.history().first().toMap().value("responseSeconds").toDouble(),frozen);
}
void CoreTest::invalidGradesLeaveReviewUnchanged()
{
    QTemporaryDir directory;AppController app(directory.path());QVERIFY(idle(app));const QString deckId=addDeck(app);
    QVERIFY(!addCard(app,deckId,"basic","Question","Answer",3).isEmpty());app.startReview(deckId);QVERIFY(idle(app));app.revealAnswer();
    const QVariantList schedules=app.cards();
    for (const int grade:{-1,5,100}) {app.grade(grade);QCOMPARE(app.lastError().value("code").toString(),QStringLiteral("INVALID_GRADE"));QVERIFY(!app.busy());}
    for (const double fraction:{-.1,1.1,std::numeric_limits<double>::quiet_NaN(),std::numeric_limits<double>::infinity()}) {
        app.grade(1,fraction);QCOMPARE(app.lastError().value("code").toString(),QStringLiteral("INVALID_GRADE"));QVERIFY(!app.busy());
    }
    QCOMPARE(app.cards(),schedules);QCOMPARE(app.reviewedCount(),0);QVERIFY(app.history().isEmpty());QCOMPARE(app.queueCount(),1);
    app.grade(1,2.0/3.0);QVERIFY(idle(app));QCOMPARE(app.history().first().toMap().value("recallFraction").toDouble(),2.0/3.0);
}
void CoreTest::mutationsAndOutboxRollBackTogether()
{
    QTemporaryDir directory;AppController app(directory.path());QVERIFY(idle(app));const QString deckId=addDeck(app);
    QVERIFY(!addCard(app,deckId,"basic","Question","Answer").isEmpty());app.startReview(deckId);QVERIFY(idle(app));app.revealAnswer();
    const QVariantList originalCards=app.cards(),originalEvents=batch(app).value("events").toList();
    {
        struct DatabaseAccess db(app.storagePath());QVERIFY(db.run("CREATE TRIGGER fail_outbox BEFORE INSERT ON sync_outbox BEGIN SELECT RAISE(ABORT,'simulated storage failure'); END"));
        app.grade(3);QVERIFY(idle(app));QCOMPARE(app.lastError().value("code").toString(),QStringLiteral("REVIEW_SAVE"));
        QVERIFY(app.lastError().value("detail").toString().contains("simulated storage failure"));QCOMPARE(app.reviewedCount(),0);QCOMPARE(app.queueCount(),1);
        app.refresh();QVERIFY(idle(app));QCOMPARE(app.cards(),originalCards);QVERIFY(app.history().isEmpty());
        QCOMPARE(batch(app).value("events").toList(),originalEvents);QCOMPARE(db.count("reviews"),0);
        QVERIFY(db.run("DROP TRIGGER fail_outbox"));
    }
    app.grade(3);QVERIFY(idle(app));QCOMPARE(app.reviewedCount(),1);QCOMPARE(app.history().size(),1);
}
void CoreTest::staleQueueEntriesAreRemoved()
{
    QTemporaryDir directory;AppController app(directory.path());QVERIFY(idle(app));const QString deckId=addDeck(app);
    QVERIFY(!addCard(app,deckId,"basic","One","A").isEmpty());QVERIFY(!addCard(app,deckId,"basic","Two","B").isEmpty());
    app.startReview(deckId);QVERIFY(idle(app));const QString deleted=app.currentCard().value("id").toString();
    app.deleteCard(deleted);QVERIFY(idle(app));QCOMPARE(app.queueCount(),1);QVERIFY(app.currentCard().value("id").toString()!=deleted);
    QCOMPARE(app.reviewedCount(),0);app.deleteDeck(deckId);QVERIFY(idle(app));QVERIFY(!app.reviewing());QVERIFY(app.pendingCards().isEmpty());
}
void CoreTest::exportImportAndValidation()
{
    QTemporaryDir sourceDirectory,destinationDirectory;AppController source(sourceDirectory.path()),destination(destinationDirectory.path());QVERIFY(idle(source));QVERIFY(idle(destination));
    const QString deckId=addDeck(source);QVERIFY(!addCard(source,deckId,"reverse","Original front","Original back").isEmpty());
    QVERIFY(!addCard(source,deckId,"cloze","{{c1::One}} and {{c2::Two}}",{},2).isEmpty());
    source.startReview(deckId);QVERIFY(idle(source));source.revealAnswer();source.grade(1,.5);QVERIFY(idle(source));
    const QString exportPath=sourceDirectory.filePath("collection.json");source.exportCollection(exportPath);QVERIFY(idle(source));QVERIFY(source.lastError().isEmpty());
    const QJsonObject valid=readJson(exportPath);QCOMPARE(valid.value("version").toInt(),2);QCOMPARE(valid.value("variants").toArray().size(),4);
    destination.importCollection(exportPath);QVERIFY(idle(destination));QVERIFY2(destination.lastError().isEmpty(),qPrintable(destination.lastError().value("detail").toString()));
    QCOMPARE(destination.cards(),source.cards());QCOMPARE(destination.history(),source.history());QCOMPARE(destination.decks(),source.decks());
    const QVariantList originalEvents=batch(destination).value("events").toList();
    destination.importCollection(exportPath);QVERIFY(idle(destination));QVERIFY(destination.lastError().isEmpty());
    QCOMPARE(batch(destination).value("events").toList(),originalEvents);
    const QString badPath=sourceDirectory.filePath("bad.json");
    QList<QJsonObject> malformed;
    {
        QJsonObject bad=valid;QJsonArray cards=bad.value("cards").toArray();cards.append(cards.first());bad.insert("cards",cards);malformed.append(bad);
    }
    {
        QJsonObject bad=valid;QJsonArray cards=bad.value("cards").toArray();QJsonObject note=cards.first().toObject();note.insert("deckId",model::uuid());cards[0]=note;bad.insert("cards",cards);malformed.append(bad);
    }
    {
        QJsonObject bad=valid;QJsonArray schedules=bad.value("variants").toArray();QJsonObject schedule=schedules.first().toObject();schedule.insert("stability",std::numeric_limits<double>::infinity());schedules[0]=schedule;bad.insert("variants",schedules);malformed.append(bad);
    }
    {
        QJsonObject bad=valid;QJsonArray cards=bad.value("cards").toArray();QJsonObject note=cards.first().toObject();note.insert("front",QString(model::maxTextLength+1,QLatin1Char('x')));cards[0]=note;bad.insert("cards",cards);malformed.append(bad);
    }
    {
        QJsonObject bad=valid;QJsonArray history=bad.value("history").toArray();QJsonObject review=history.first().toObject();review.insert("cardId",model::uuid());history[0]=review;bad.insert("history",history);malformed.append(bad);
    }
    {
        QJsonObject bad=valid;QJsonArray schedules=bad.value("variants").toArray();schedules.removeLast();bad.insert("variants",schedules);malformed.append(bad);
    }
    for (const QJsonObject &bad:malformed) {
        QVERIFY(writeJson(badPath,bad));destination.importCollection(badPath);QVERIFY(idle(destination));
        QCOMPARE(destination.lastError().value("code").toString(),QStringLiteral("INVALID_IMPORT"));
        QCOMPARE(destination.cards(),source.cards());QCOMPARE(destination.history(),source.history());QCOMPARE(batch(destination).value("events").toList(),originalEvents);
    }
    QJsonObject collision=valid;QJsonArray decks=collision.value("decks").toArray();QJsonObject changed=decks.first().toObject();changed.insert("name","Conflicting name");decks[0]=changed;collision.insert("decks",decks);
    QVERIFY(writeJson(badPath,collision));destination.importCollection(badPath);QVERIFY(idle(destination));QCOMPARE(destination.lastError().value("code").toString(),QStringLiteral("IMPORT_COLLISION"));
    QCOMPARE(destination.decks(),source.decks());
    destination.exportCollection(destinationDirectory.path());QVERIFY(idle(destination));QCOMPARE(destination.lastError().value("code").toString(),QStringLiteral("EXPORT_WRITE"));
    destination.exportCollection(destination.storagePath());QVERIFY(idle(destination));QCOMPARE(destination.lastError().value("code").toString(),QStringLiteral("INVALID_EXPORT_PATH"));
}
void CoreTest::importRollsBackOnStorageFailure()
{
    QTemporaryDir sourceDirectory,destinationDirectory;AppController source(sourceDirectory.path()),destination(destinationDirectory.path());QVERIFY(idle(source));QVERIFY(idle(destination));
    const QString deckId=addDeck(source);QVERIFY(!addCard(source,deckId,"basic","Question","Answer").isEmpty());
    const QString exportPath=sourceDirectory.filePath("collection.json");source.exportCollection(exportPath);QVERIFY(idle(source));
    struct DatabaseAccess db(destination.storagePath());QVERIFY(db.run("CREATE TRIGGER fail_outbox BEFORE INSERT ON sync_outbox BEGIN SELECT RAISE(ABORT,'import failure'); END"));
    destination.importCollection(exportPath);QVERIFY(idle(destination));QCOMPARE(destination.lastError().value("code").toString(),QStringLiteral("IMPORT_ROLLBACK"));
    destination.refresh();QVERIFY(idle(destination));QVERIFY(destination.cards().isEmpty());QVERIFY(destination.decks().isEmpty());QCOMPARE(db.count("sync_outbox"),0);
    QVERIFY(db.run("DROP TRIGGER fail_outbox"));destination.importCollection(exportPath);QVERIFY(idle(destination));QCOMPARE(destination.cards().size(),1);
}
void CoreTest::retainedDeletedHistoryRoundTrips()
{
    QTemporaryDir sourceDirectory,destinationDirectory;AppController source(sourceDirectory.path()),destination(destinationDirectory.path());QVERIFY(idle(source));QVERIFY(idle(destination));
    const QString deckId=addDeck(source);const QString id=addCard(source,deckId,"basic","Question","Answer");
    source.startReview(deckId);QVERIFY(idle(source));source.revealAnswer();source.grade(3);QVERIFY(idle(source));
    source.deleteDeck(deckId);QVERIFY(idle(source));QVERIFY(source.cards().isEmpty());QCOMPARE(source.history().size(),1);
    const QString exportPath=sourceDirectory.filePath("deleted.json");source.exportCollection(exportPath);QVERIFY(idle(source));
    destination.importCollection(exportPath);QVERIFY(idle(destination));QVERIFY2(destination.lastError().isEmpty(),qPrintable(destination.lastError().value("detail").toString()));
    QVERIFY(destination.cards().isEmpty());QCOMPARE(destination.history(),source.history());QCOMPARE(destination.history().first().toMap().value("cardId").toString(),id);
    const QVariantList events=batch(destination).value("events").toList();bool foundHistory=false;
    for (const QVariant &value:events) if (value.toMap().value("type")==QStringLiteral("review.add")) foundHistory=true;
    QVERIFY(foundHistory);
}
void CoreTest::mediaBackupsRoundTripAndRejectUnsafeData()
{
    QTemporaryDir sourceDirectory,destinationDirectory,unsafeDirectory,symlinkDirectory,rollbackDirectory,opaqueDirectory;
    AppController source(sourceDirectory.path()),destination(destinationDirectory.path());QVERIFY(idle(source));QVERIFY(idle(destination));
    const QByteArray image=tinyPng();
    const QString name=QString::fromLatin1(QCryptographicHash::hash(image,QCryptographicHash::Sha256).toHex())+".png";
    QVERIFY(QDir().mkpath(source.mediaPath()));
    const QString imagePath=QDir(source.mediaPath()).filePath(name);
    {QFile file(imagePath);QVERIFY(file.open(QIODevice::WriteOnly));QCOMPARE(file.write(image),image.size());}
    const QString deckId=addDeck(source);
    const QString front=QStringLiteral("Question image\n\n![Front](media:%1)").arg(name);
    const QString back=QStringLiteral("Answer image\n\n![Back](media:%1)").arg(name);
    QVERIFY(!addCard(source,deckId,"basic",front,back).isEmpty());
    const QString path=sourceDirectory.filePath("images.json");source.exportCollection(path);QVERIFY(idle(source));QVERIFY(source.lastError().isEmpty());
    const QJsonObject valid=readJson(path);QCOMPARE(valid.value("media").toArray().size(),1);
    destination.importCollection(path);QVERIFY(idle(destination));QVERIFY2(destination.lastError().isEmpty(),qPrintable(destination.lastError().value("detail").toString()));
    QCOMPARE(destination.cards(),source.cards());
    {QFile file(QDir(destination.mediaPath()).filePath(name));QVERIFY(file.open(QIODevice::ReadOnly));QCOMPARE(file.readAll(),image);}
    {
        const QByteArray opaqueBytes=QByteArrayLiteral("opaque bytes with a valid digest");
        const QString opaqueName=QString::fromLatin1(QCryptographicHash::hash(opaqueBytes,QCryptographicHash::Sha256).toHex())+QStringLiteral(".png");
        QJsonObject malformed=valid;
        QJsonArray opaqueCards=malformed.value("cards").toArray();
        for (int index=0;index<opaqueCards.size();++index) {
            QJsonObject card=opaqueCards.at(index).toObject();
            card.insert("front",card.value("front").toString().replace(name,opaqueName));
            card.insert("back",card.value("back").toString().replace(name,opaqueName));
            opaqueCards[index]=card;
        }
        malformed.insert("cards",opaqueCards);
        malformed.insert("media",QJsonArray{QJsonObject{{"name",opaqueName},{"data",QString::fromLatin1(opaqueBytes.toBase64())}}});
        const QString opaquePath=sourceDirectory.filePath("opaque-image.json");QVERIFY(writeJson(opaquePath,malformed));
        AppController opaque(opaqueDirectory.path());QVERIFY(idle(opaque));
        opaque.importCollection(opaquePath);QVERIFY(idle(opaque));
        QCOMPARE(opaque.lastError().value("code").toString(),QStringLiteral("INVALID_IMPORT"));
        QVERIFY(opaque.cards().isEmpty());QVERIFY(opaque.decks().isEmpty());
        struct DatabaseAccess db(opaque.storagePath());QCOMPARE(db.count("sync_outbox"),0);
        QVERIFY(!QFileInfo::exists(QDir(opaque.mediaPath()).filePath(opaqueName)));
    }
    const QString badPath=sourceDirectory.filePath("bad-images.json");
    QList<QJsonObject> invalid;
    for (const QString &field:QStringList{"hash","filename","base64","missing","duplicate"}) {
        QJsonObject bad=valid;QJsonArray entries=bad.value("media").toArray();QJsonObject entry=entries.first().toObject();
        if (field=="hash") entry.insert("data",QString::fromLatin1(QByteArray("tampered").toBase64()));
        else if (field=="filename") entry.insert("name","../../outside.png");
        else if (field=="base64") entry.insert("data",entry.value("data").toString()+"$");
        if (field=="missing") entries=QJsonArray{};
        else if (field=="duplicate") entries.append(entry);
        else entries[0]=entry;
        bad.insert("media",entries);invalid.append(bad);
    }
    for (const QJsonObject &bad:invalid) {
        QVERIFY(writeJson(badPath,bad));destination.importCollection(badPath);QVERIFY(idle(destination));
        QCOMPARE(destination.lastError().value("code").toString(),QStringLiteral("INVALID_IMPORT"));QCOMPARE(destination.cards(),source.cards());
    }
    {
        AppController unsafe(unsafeDirectory.path());QVERIFY(idle(unsafe));QVERIFY(QDir().mkpath(unsafe.mediaPath()));
        const QString target=QDir(unsafe.mediaPath()).filePath(name);
        {QFile file(target);QVERIFY(file.open(QIODevice::WriteOnly));QVERIFY(file.write("untrusted")>0);}
        unsafe.importCollection(path);QVERIFY(idle(unsafe));QCOMPARE(unsafe.lastError().value("code").toString(),QStringLiteral("IMPORT_MEDIA"));QVERIFY(unsafe.cards().isEmpty());
        QFile file(target);QVERIFY(file.open(QIODevice::ReadOnly));QCOMPARE(file.readAll(),QByteArray("untrusted"));
    }
    {
        AppController symlinked(symlinkDirectory.path());QVERIFY(idle(symlinked));QVERIFY(QDir().mkpath(symlinked.mediaPath()));
        const QString outside=symlinkDirectory.filePath("outside");
        {QFile file(outside);QVERIFY(file.open(QIODevice::WriteOnly));QCOMPARE(file.write(image),image.size());}
        QVERIFY(QFile::link(outside,QDir(symlinked.mediaPath()).filePath(name)));
        symlinked.importCollection(path);QVERIFY(idle(symlinked));QCOMPARE(symlinked.lastError().value("code").toString(),QStringLiteral("IMPORT_MEDIA"));QVERIFY(symlinked.cards().isEmpty());
    }
    {
        AppController rollback(rollbackDirectory.path());QVERIFY(idle(rollback));struct DatabaseAccess db(rollback.storagePath());
        QVERIFY(db.run("CREATE TRIGGER fail_outbox BEFORE INSERT ON sync_outbox BEGIN SELECT RAISE(ABORT,'image import DB failure'); END"));
        rollback.importCollection(path);QVERIFY(idle(rollback));QCOMPARE(rollback.lastError().value("code").toString(),QStringLiteral("IMPORT_ROLLBACK"));
        QVERIFY(rollback.lastError().value("message").toString().contains("Unused verified image files"));QCOMPARE(db.count("notes"),0);
        QFile imageFile(QDir(rollback.mediaPath()).filePath(name));QVERIFY(imageFile.open(QIODevice::ReadOnly));QCOMPARE(imageFile.readAll(),image);
    }
    QVERIFY(QFile::remove(imagePath));source.exportCollection(path);QVERIFY(idle(source));QCOMPARE(source.lastError().value("code").toString(),QStringLiteral("EXPORT_MEDIA"));
    QCOMPARE(readJson(path),valid);
}
void CoreTest::preserveDamagedMediaUsesDirectoryFd()
{
    QTemporaryDir directory;
    const QByteArray image=tinyPng();
    const QString name=QString::fromLatin1(QCryptographicHash::hash(image,QCryptographicHash::Sha256).toHex())+QStringLiteral(".png");
    const auto absentRoot=model::preserveDamagedMedia(QDir(directory.path()).filePath(QStringLiteral("missing-media")),name);QVERIFY(absentRoot.has_value());QVERIFY(absentRoot->isEmpty());
    QFile original(QDir(directory.path()).filePath(name));QVERIFY(original.open(QIODevice::WriteOnly));QCOMPARE(original.write(image),image.size());original.close();
    const auto preserved=model::preserveDamagedMedia(directory.path(),name);QVERIFY(preserved.has_value());QVERIFY(preserved->startsWith(name+QStringLiteral(".damaged-")));
    QVERIFY(!QFileInfo::exists(QDir(directory.path()).filePath(name)));QVERIFY(QFileInfo(QDir(directory.path()).filePath(*preserved)).isFile());
    const auto absent=model::preserveDamagedMedia(directory.path(),name);QVERIFY(absent.has_value());QVERIFY(absent->isEmpty());
    QVERIFY(QFile::link(QDir(directory.path()).filePath(*preserved),QDir(directory.path()).filePath(name)));
    QVERIFY(!model::preserveDamagedMedia(directory.path(),name));
}

void CoreTest::syncRoundTripIdempotenceAndDeletion()
{
    QTemporaryDir aDirectory,bDirectory;AppController a(aDirectory.path()),b(bDirectory.path());QVERIFY(idle(a));QVERIFY(idle(b));struct FakeServer server;
    const QString deckId=addDeck(a),id=addCard(a,deckId,"reverse","Front","Back");
    QVERIFY(apply(a,server.exchange(batch(a))));QVERIFY(batch(a).value("events").toList().isEmpty());
    QVERIFY(apply(b,server.exchange(batch(b))));QCOMPARE(b.cards(),a.cards());
    b.startReview(deckId);QVERIFY(idle(b));b.revealAnswer();b.grade(3);QVERIFY(idle(b));
    const QVariantMap bResponse=server.exchange(batch(b));QVERIFY(apply(b,bResponse));
    const QVariantMap aResponse=server.exchange(batch(a));QVERIFY(apply(a,aResponse));QCOMPARE(a.history(),b.history());QCOMPARE(a.cards(),b.cards());
    QVERIFY(apply(a,server.exchange(batch(a))));QCOMPARE(a.history().size(),1);
    const QVariantMap remainingVariant=b.currentCard();
    b.saveCard(id,deckId,"reverse","Offline edited front","Back",{},1);QVERIFY(idle(b));
    a.deleteCard(id);QVERIFY(idle(a));QVERIFY(apply(a,server.exchange(batch(a))));
    const QVariantMap deleteResponse=server.exchange(batch(b));QVERIFY(apply(b,deleteResponse));
    QVERIFY(b.cards().isEmpty());QVERIFY(b.pendingCards().isEmpty());QCOMPARE(b.history().size(),1);
    QVERIFY(apply(a,server.exchange(batch(a))));QVERIFY(a.cards().isEmpty());QCOMPARE(a.history().size(),1);
    QVariantMap staleSchedule=variant(card(b,id),"reverse");
    staleSchedule={{"id",remainingVariant.value("variantId")},{"cardId",id},{"key",remainingVariant.value("variantKey")},
        {"due",model::nowUtc()},{"reviewCount",0},{"stability",1.0},{"difficulty",.5}};
    server.append(syncEvent("variant.upsert",{{"variant",staleSchedule}}));
    QVERIFY(apply(a,server.exchange(batch(a))));QVERIFY(a.cards().isEmpty());
}
void CoreTest::syncProtectsPendingEditsAcrossDownloadPages()
{
    QTemporaryDir directory;QString deviceId;struct FakeServer server;
    {
        AppController app(directory.path());QVERIFY(idle(app));const QString deckId=addDeck(app),id=addCard(app,deckId,"basic","Initial","Answer");
        QVERIFY(apply(app,server.exchange(batch(app))));const QVariantMap initial=card(app,id);
        for (int n=0;n<8;++n) {
            QVariantMap older=initial;older.insert("front",QStringLiteral("Remote older %1").arg(n));
            server.append(syncEvent("card.upsert",{{"card",older},{"variants",initial.value("variants")}}));
        }
        app.saveCard(id,deckId,"basic","Newest local edit","Answer",{});QVERIFY(idle(app));
        const QVariantMap first=server.exchange(batch(app),2);QCOMPARE(first.value("acceptedIds").toList().size(),1);QVERIFY(apply(app,first));
        QCOMPARE(card(app,id).value("front").toString(),QStringLiteral("Newest local edit"));
        QVariantMap request=batch(app);QVERIFY(request.value("events").toList().isEmpty());deviceId=request.value("deviceId").toString();
        while (request.value("cursor").toLongLong()<server.log.size()) {
            QVERIFY(apply(app,server.exchange(request,2)));QCOMPARE(card(app,id).value("front").toString(),QStringLiteral("Newest local edit"));request=batch(app);
        }
        struct DatabaseAccess db(app.storagePath());QCOMPARE(db.count("remote_pending"),0);
    }
    {
        AppController app(directory.path());QVERIFY(idle(app));const QVariantMap persisted=batch(app);QCOMPARE(persisted.value("deviceId").toString(),deviceId);QCOMPARE(persisted.value("cursor").toLongLong(),static_cast<qint64>(server.log.size()));
        QCOMPARE(app.cards().first().toMap().value("front").toString(),QStringLiteral("Newest local edit"));
    }
}
void CoreTest::syncMissingDependenciesAreDurable()
{
    QTemporaryDir directory;const struct model::Deck deck{model::uuid(),"Remote deck",{},model::nowUtc()};
    const struct model::Card note{model::uuid(),deck.id,"basic","Question","Answer",{},1};
    const struct model::Variant initial{model::variantId(note.id,"forward"),note.id,"forward",model::nowUtc()};
    struct model::Variant reviewed=initial;reviewed.reviewCount=1;reviewed.due=QDateTime::currentDateTimeUtc().addDays(5).toString(Qt::ISODateWithMs);
    const struct model::Review review{model::uuid(),note.id,initial.id,deck.name,3,1,4,model::nowUtc(),reviewed.due};
    {
        AppController app(directory.path());QVERIFY(idle(app));
        QVERIFY(!batch(app).isEmpty());
        QVERIFY(apply(app,response({QVariantMap{{"seq",1},{"event",syncEvent("card.upsert",{{"card",model::toMap(note)},{"variants",QVariantList{model::toMap(initial)}}})}}},1)));
        QVERIFY(app.cards().isEmpty());struct DatabaseAccess db(app.storagePath());QCOMPARE(db.count("remote_pending"),1);
    }
    {
        AppController app(directory.path());QVERIFY(idle(app));QCOMPARE(batch(app).value("cursor").toLongLong(),1);
        const QVariantList events{QVariantMap{{"seq",2},{"event",syncEvent("review.add",{{"review",model::toMap(review)},{"variant",model::toMap(reviewed)}})}},
            QVariantMap{{"seq",3},{"event",syncEvent("deck.upsert",{{"deck",model::toMap(deck)}})}}};
        QVERIFY(apply(app,response(events,3)));QCOMPARE(app.cards().size(),1);QCOMPARE(app.history().size(),1);
        QCOMPARE(variant(card(app,note.id),"forward").value("reviewCount").toInt(),1);
        QCOMPARE(variant(card(app,note.id),"forward").value("due").toString(),reviewed.due);
        struct DatabaseAccess db(app.storagePath());QCOMPARE(db.count("remote_pending"),0);
        QVERIFY(!batch(app).isEmpty());
        const QVariantList deletes{QVariantMap{{"seq",4},{"event",syncEvent("deck.delete",{{"id",deck.id}})}},
            QVariantMap{{"seq",5},{"event",syncEvent("card.upsert",{{"card",model::toMap(note)},{"variants",QVariantList{model::toMap(initial)}}})}}};
        QVERIFY(apply(app,response(deletes,5)));QVERIFY(app.cards().isEmpty());QCOMPARE(app.history().size(),1);
    }
}
void CoreTest::invalidSyncDoesNotAcknowledgeUploads()
{
    QTemporaryDir directory;AppController app(directory.path());QVERIFY(idle(app));const QString deckId=addDeck(app);const QString id=addCard(app,deckId,"basic","Question","Answer");
    const QVariantList events=batch(app).value("events").toList();QVariantList ids;for (const QVariant &v:events) ids.append(v.toMap().value("id"));
    QVariantMap invalid=variant(card(app,id),"forward");invalid.insert("stability",std::numeric_limits<double>::quiet_NaN());
    const QVariantMap corrupt=response({QVariantMap{{"seq",1},{"event",syncEvent("variant.upsert",{{"variant",invalid}})}}},1,ids);
    QVERIFY(!apply(app,corrupt));QCOMPARE(app.lastError().value("code").toString(),QStringLiteral("INVALID_SYNC_RESPONSE"));QVERIFY(!app.busy());
    QCOMPARE(batch(app).value("events").toList(),events);QCOMPARE(batch(app).value("cursor").toLongLong(),0);
    const QVariantMap deletion=syncEvent("card.delete",{{"id",id}});
    QVERIFY(!apply(app,response({QVariantMap{{"seq",1},{"event",deletion}},QVariantMap{{"seq",2},{"event",deletion}}},2,ids)));
    QCOMPARE(app.cards().size(),1);QCOMPARE(batch(app).value("events").toList(),events);
}
void CoreTest::syncRequiresIssuedAcknowledgmentsAndContiguousCursor()
{
    QTemporaryDir directory;AppController app(directory.path());QVERIFY(idle(app));QVERIFY(!addDeck(app,"Issued").isEmpty());
    const QVariantMap issued=batch(app);QCOMPARE(issued.value("events").toList().size(),1);
    const QString issuedId=issued.value("events").toList().first().toMap().value("id").toString();
    QVERIFY(!addDeck(app,"Created after issue").isEmpty());
    struct DatabaseAccess db(app.storagePath());QString laterId;
    {QSqlQuery query(db.db);QVERIFY(query.exec("SELECT id FROM sync_outbox ORDER BY position DESC LIMIT 1"));QVERIFY(query.next());laterId=query.value(0).toString();}
    QVERIFY(laterId!=issuedId);
    QVERIFY(!apply(app,response({},0,{laterId})));QCOMPARE(app.lastError().value("code").toString(),QStringLiteral("INVALID_SYNC_RESPONSE"));QCOMPARE(db.count("sync_outbox"),2);
    const QVariantMap remote=syncEvent("deck.delete",{{"id",model::uuid()}});
    QVERIFY(!apply(app,response({QVariantMap{{"seq",2},{"event",remote}}},2,{issuedId})));QCOMPARE(db.count("sync_outbox"),2);
    QVERIFY(!apply(app,response({},100,{issuedId})));QCOMPARE(db.count("sync_outbox"),2);
    QVERIFY(!apply(app,response({QVariantMap{{"seq",1},{"event",remote}}},2,{issuedId})));QCOMPARE(db.count("sync_outbox"),2);
    QCOMPARE(db.count("seen_events"),2);QCOMPARE(db.count("tombstones"),0);
    const QVariantMap pending=batch(app);QCOMPARE(pending.value("cursor").toLongLong(),0);QCOMPARE(pending.value("events").toList().size(),2);
    struct FakeServer server;QVERIFY(apply(app,server.exchange(pending)));QCOMPARE(db.count("sync_outbox"),0);
    QCOMPARE(batch(app).value("cursor").toLongLong(),2);
}

void CoreTest::syncByteCapsAndFailureSignals()
{
    QTemporaryDir directory;AppController app(directory.path());QVERIFY(idle(app));const QString deckId=addDeck(app);
    const QString content(850000,QLatin1Char('a'));
    for (int n=0;n<3;++n) QVERIFY(!addCard(app,deckId,"basic",QString::number(n)+content,content).isEmpty());
    const QVariantMap first=batch(app);QVERIFY(first.value("hasMoreLocal").toBool());
    QVERIFY(QJsonDocument(QJsonObject::fromVariantMap(first)).toJson(QJsonDocument::Compact).size()<=model::maxSyncBatchBytes);
    QVERIFY(first.value("events").toList().size()<4);struct FakeServer server;QVERIFY(apply(app,server.exchange(first)));
    const QVariantMap last=batch(app);QVERIFY(!last.value("hasMoreLocal").toBool());QCOMPARE(last.value("events").toList().size(),1);
    const QVariantList records=app.cards();
    const QString oversized(model::maxTextLength,QChar(0x754c));
    app.saveCard({},deckId,"basic",oversized,oversized,{});QVERIFY(idle(app));QCOMPARE(app.lastError().value("code").toString(),QStringLiteral("CARD_SAVE"));
    QVERIFY(app.lastError().value("detail").toString().contains("Shorten card content"));QCOMPARE(app.cards(),records);
    QCOMPARE(batch(app).value("events").toList(),last.value("events").toList());
    {
        struct DatabaseAccess db(app.storagePath());QVERIFY(db.run("DROP TABLE sync_outbox"));QSignalSpy failure(&app,&AppController::syncBatchFailed);
        app.requestSyncBatch();QVERIFY(idle(app));QCOMPARE(failure.size(),1);QCOMPARE(failure.first().first().toString(),QStringLiteral("SYNC_READ"));
    }
    QTemporaryDir invalidDirectory;const QString regularFile=invalidDirectory.filePath("regular-file");
    {QFile file(regularFile);QVERIFY(file.open(QIODevice::WriteOnly));QVERIFY(file.write("x")>0);}
    AppController unavailable(regularFile);QVERIFY(idle(unavailable));QCOMPARE(unavailable.lastError().value("code").toString(),QStringLiteral("STORAGE_DIRECTORY"));
    QSignalSpy failure(&unavailable,&AppController::syncBatchFailed);unavailable.requestSyncBatch();QVERIFY(idle(unavailable));QCOMPARE(failure.size(),1);
    QCOMPARE(failure.first().first().toString(),QStringLiteral("STORAGE_UNAVAILABLE"));
}

void CoreTest::syncRequestCorrelationAndTransactionalRollback()
{
    QTemporaryDir directory;AppController app(directory.path());QVERIFY(idle(app));QVERIFY(!addDeck(app,"Local").isEmpty());
    const QString firstId=model::uuid(),secondId=model::uuid();
    const QVariantMap first=batch(app,firstId),second=batch(app,secondId);
    QCOMPARE(first.value("requestId").toString(),firstId);QCOMPARE(second.value("requestId").toString(),secondId);
    struct FakeServer server;
    const struct model::Deck remoteDeck{model::uuid(),"Remote",QStringLiteral(""),model::nowUtc()};
    const struct model::Card remoteCard{model::uuid(),remoteDeck.id,"basic","Remote question","Remote answer",QStringLiteral(""),1};
    const struct model::Variant remoteVariant{model::variantId(remoteCard.id,"forward"),remoteCard.id,"forward",model::nowUtc()};
    QVariantMap valid=server.exchange(first);
    server.append(syncEvent("deck.upsert",{{"deck",model::toMap(remoteDeck)}}));
    server.append(syncEvent("card.upsert",{{"card",model::toMap(remoteCard)},{"variants",QVariantList{model::toMap(remoteVariant)}}}));
    valid=server.exchange(first);
    struct DatabaseAccess db(app.storagePath());
    QVERIFY(db.run("CREATE TRIGGER fail_remote_note BEFORE INSERT ON notes BEGIN SELECT RAISE(ABORT,'remote failure'); END"));
    QSignalSpy applied(&app,&AppController::syncResponseApplied);
    QVERIFY(!apply(app,valid));QCOMPARE(app.lastError().value("code").toString(),QStringLiteral("SYNC_APPLY"));
    QCOMPARE(applied.size(),1);QCOMPARE(applied.first().at(0).toString(),firstId);QVERIFY(!applied.first().at(1).toBool());
    QCOMPARE(db.count("sync_outbox"),1);QCOMPARE(db.count("seen_events"),1);QCOMPARE(db.count("remote_pending"),0);
    QCOMPARE(app.decks().size(),1);QVERIFY(app.cards().isEmpty());
    QVariantMap unknown=valid;unknown.insert("requestId",model::uuid());
    QVERIFY(!apply(app,unknown));QCOMPARE(app.lastError().value("code").toString(),QStringLiteral("INVALID_SYNC_RESPONSE"));
    QCOMPARE(db.count("sync_outbox"),1);QCOMPARE(db.count("remote_pending"),0);
    QVERIFY(db.run("DROP TRIGGER fail_remote_note"));QVERIFY(apply(app,valid));
    QCOMPARE(app.decks().size(),2);QCOMPARE(app.cards().size(),1);QCOMPARE(db.count("sync_outbox"),0);
    QVariantMap stale=server.exchange(second);QVERIFY(!apply(app,stale));
    QCOMPARE(applied.last().at(0).toString(),secondId);QVERIFY(!applied.last().at(1).toBool());
    QCOMPARE(app.lastError().value("code").toString(),QStringLiteral("INVALID_SYNC_RESPONSE"));
    QCOMPARE(batch(app,model::uuid()).value("cursor").toLongLong(),3);
    const QVariantMap noProgress=response({},3);QVariantMap endless=noProgress;endless.insert("hasMore",true);
    QVERIFY(!betterflash::protocol::validateResponse(endless,3,{}));
    const QString rejectedId(129,QLatin1Char('a'));QSignalSpy failed(&app,&AppController::syncBatchFailedForRequest);
    app.requestSyncBatch(rejectedId);QVERIFY(idle(app));QCOMPARE(failed.size(),1);QCOMPARE(failed.first().at(0).toString(),rejectedId);
    QCOMPARE(failed.first().at(1).toString(),QStringLiteral("INVALID_SYNC_REQUEST"));
}

void CoreTest::syncMediaBatchesSplitBeforeLimits()
{
    QTemporaryDir directory,importDirectory;AppController app(directory.path());QVERIFY(idle(app));
    const QString deckId=addDeck(app);QVERIFY(QDir().mkpath(app.mediaPath()));
    const auto writeImage=[&](const QByteArray &bytes) {
        const QString name=QString::fromLatin1(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex())+".png";
        QFile file(QDir(app.mediaPath()).filePath(name));
        if (!file.open(QIODevice::WriteOnly)||file.write(bytes)!=bytes.size()) return QString();
        return name;
    };
    QString allReferences;
    for (int n=0;n<65;++n) {
        QByteArray image=tinyPng();image.append(QByteArray::number(n));
        const QString name=writeImage(image);QVERIFY(!name.isEmpty());
        const QString front=QStringLiteral("Image %1\n![Diagram](media:%2)").arg(n).arg(name);allReferences+=front+"\n";
        QVERIFY(!addCard(app,deckId,"basic",front,"Answer").isEmpty());
    }
    const QVariantList original=app.cards();
    QVERIFY(!app.saveCard({},deckId,"basic",allReferences,"Answer",{}));QVERIFY(!app.busy());
    QCOMPARE(app.lastError().value("code").toString(),QStringLiteral("INVALID_CARD"));QCOMPARE(app.cards(),original);
    struct FakeServer server;const QVariantMap first=batch(app);QVERIFY(first.value("hasMoreLocal").toBool());
    QCOMPARE(first.value("events").toList().size(),65);QVERIFY(apply(app,server.exchange(first)));
    const QVariantMap last=batch(app);QVERIFY(!last.value("hasMoreLocal").toBool());QCOMPARE(last.value("events").toList().size(),1);
    QVERIFY(apply(app,server.exchange(last)));
    const QString backup=directory.filePath("images.json");app.exportCollection(backup);QVERIFY(idle(app));QVERIFY(app.lastError().isEmpty());
    QJsonObject oversized=readJson(backup);QJsonArray cards=oversized.value("cards").toArray();QJsonObject note=cards.first().toObject();
    note.insert("front",allReferences);cards[0]=note;oversized.insert("cards",cards);QVERIFY(writeJson(backup,oversized));
    AppController destination(importDirectory.path());QVERIFY(idle(destination));destination.importCollection(backup);QVERIFY(idle(destination));
    QCOMPARE(destination.lastError().value("code").toString(),QStringLiteral("INVALID_IMPORT"));QVERIFY(destination.cards().isEmpty());
    QVERIFY(!QDir(destination.mediaPath()).exists());
    const QString missingName=QString::fromLatin1(QCryptographicHash::hash("missing",QCryptographicHash::Sha256).toHex())+".png";
    QVERIFY(app.saveCard({},deckId,"basic",QStringLiteral("![Missing](media:%1)").arg(missingName),"Answer",{}));QVERIFY(idle(app));
    QCOMPARE(app.lastError().value("code").toString(),QStringLiteral("CARD_MEDIA"));QCOMPARE(app.cards(),original);
    QVERIFY(batch(app).value("events").toList().isEmpty());
    QString largeReferences;
    for (int n=0;n<4;++n) {
        QByteArray bytes=tinyPng();bytes.append(QByteArray(17*1024*1024-bytes.size(),static_cast<char>('a'+n)));
        const QString name=writeImage(bytes);QVERIFY(!name.isEmpty());
        const QString front=QStringLiteral("Large image %1\n![Diagram](media:%2)").arg(n).arg(name);largeReferences+=front+"\n";
        QVERIFY(!addCard(app,deckId,"basic",front,"Answer").isEmpty());
    }
    const QVariantList withLargeImages=app.cards();
    QVERIFY(app.saveCard({},deckId,"basic",largeReferences,"Answer",{}));QVERIFY(idle(app));
    QCOMPARE(app.lastError().value("code").toString(),QStringLiteral("CARD_MEDIA"));QCOMPARE(app.cards(),withLargeImages);
    const QVariantMap limited=batch(app);QVERIFY(limited.value("hasMoreLocal").toBool());QCOMPARE(limited.value("events").toList().size(),3);
    QVERIFY(apply(app,server.exchange(limited)));const QVariantMap remaining=batch(app);
    QVERIFY(!remaining.value("hasMoreLocal").toBool());QCOMPARE(remaining.value("events").toList().size(),1);QVERIFY(apply(app,server.exchange(remaining)));
    QVERIFY(batch(app).value("events").toList().isEmpty());
}

void CoreTest::atomicReplacementIsDurableAndPreservesHistory()
{
    QTemporaryDir directory,peerDirectory;QString sourceId,deckId;QStringList replacementIds;QVariantList retainedHistory;
    {
        AppController app(directory.path()),peer(peerDirectory.path());QVERIFY(idle(app));QVERIFY(idle(peer));deckId=addDeck(app);
        sourceId=addCard(app,deckId,"reverse","Name two transaction properties","- Atomicity\n- Durability",2);
        QVERIFY(app.saveCard(sourceId,deckId,"reverse","Name two transaction properties","- Atomicity\n- Durability","storage, review",2));QVERIFY(idle(app));
        const QString sibling=addCard(app,deckId,"basic","Another question","Another answer");
        app.startReview(deckId);QVERIFY(idle(app));
        for (int n=0;n<3&&app.currentCard().value("id").toString()!=sourceId;++n) {app.deferCard();QVERIFY(idle(app));}
        QCOMPARE(app.currentCard().value("id").toString(),sourceId);app.revealAnswer();app.grade(1,.5);QVERIFY(idle(app));
        QCOMPARE(app.reviewedCount(),1);QCOMPARE(app.queueCount(),2);retainedHistory=app.history();QCOMPARE(retainedHistory.size(),1);
        struct FakeServer server;QVERIFY(apply(app,server.exchange(batch(app))));QVERIFY(apply(peer,server.exchange(batch(peer))));
        const QVariantMap expected=card(app,sourceId);
        const QVariantList proposals{QVariantMap{{"front","What does atomicity mean?"},{"back","A transaction commits its changes together."}},
            QVariantMap{{"front","How is durable storage maintained?"},{"back","- A committed write survives restart.\n- A backup preserves the collection."},{"pointCount",2}}};
        QSignalSpy split(&app,&AppController::atomicSplitApplied);QVERIFY(app.replaceCardWithAtomicCards(expected,proposals));QVERIFY(idle(app));
        QCOMPARE(split.size(),1);QCOMPARE(split.first().at(0).toString(),sourceId);QVERIFY(split.first().at(1).toBool());
        replacementIds=split.first().at(2).toStringList();QCOMPARE(replacementIds.size(),2);QVERIFY(!replacementIds.contains(sourceId));
        QVERIFY(card(app,sourceId).isEmpty());QCOMPARE(app.cards().size(),3);QCOMPARE(app.history(),retainedHistory);QCOMPARE(app.reviewedCount(),1);
        QCOMPARE(app.queueCount(),1);QCOMPARE(app.currentCard().value("id").toString(),sibling);QCOMPARE(app.pendingCards().size(),1);
        for (int n=0;n<replacementIds.size();++n) {
            const QVariantMap replacement=card(app,replacementIds.at(n));QCOMPARE(replacement.value("kind").toString(),QStringLiteral("basic"));
            QCOMPARE(replacement.value("deckId").toString(),deckId);QCOMPARE(replacement.value("tags").toString(),QStringLiteral("storage, review"));
            QCOMPARE(replacement.value("pointCount").toInt(),n+1);const QVariantMap schedule=variant(replacement,"forward");
            QCOMPARE(replacement.value("variants").toList().size(),1);QCOMPARE(schedule.value("reviewCount").toInt(),0);
            QVERIFY(QDateTime::fromString(schedule.value("due").toString(),Qt::ISODateWithMs)<=QDateTime::currentDateTimeUtc());
        }
        const QVariantMap changes=batch(app);const QVariantList events=changes.value("events").toList();QCOMPARE(events.size(),3);
        QCOMPARE(events.first().toMap().value("type").toString(),QStringLiteral("card.delete"));
        for (int n=1;n<3;++n) QCOMPARE(events.at(n).toMap().value("type").toString(),QStringLiteral("card.upsert"));
        QVERIFY(apply(app,server.exchange(changes)));QVERIFY(apply(peer,server.exchange(batch(peer))));
        QCOMPARE(peer.cards(),app.cards());QCOMPARE(peer.history(),retainedHistory);
        app.stopReview();QVERIFY(idle(app));app.startReview(deckId);QVERIFY(idle(app));QCOMPARE(app.queueCount(),3);
    }
    AppController restored(directory.path());QVERIFY(idle(restored));QVERIFY(card(restored,sourceId).isEmpty());QCOMPARE(restored.history(),retainedHistory);
    for (const QString &id:replacementIds) QVERIFY(!card(restored,id).isEmpty());
    restored.startReview(deckId);QVERIFY(idle(restored));QCOMPARE(restored.queueCount(),3);
}

void CoreTest::atomicReplacementRejectsInvalidStaleAndFailedMutations()
{
    QTemporaryDir directory;AppController app(directory.path());QVERIFY(idle(app));const QString deckId=addDeck(app);
    const QString sourceId=addCard(app,deckId,"reverse","Combined question","Combined answer",2);app.startReview(deckId);QVERIFY(idle(app));
    const QVariantMap expected=card(app,sourceId);const QVariantList original=app.cards(),queue=app.pendingCards();
    const QVariantList originalEvents=batch(app).value("events").toList();
    const QVariantList valid{QVariantMap{{"front","First question"},{"back","First answer"},{"pointCount",1}},
        QVariantMap{{"front","Second question"},{"back","Second answer"},{"pointCount",2}}};
    QList<QVariantList> invalid{QVariantList{},QVariantList{valid.first()},QVariantList{valid.first(),valid.first()},
        QVariantList{valid.first(),valid.last(),valid.first(),valid.last(),valid.first(),valid.last()},QVariantList{12,valid.last()}};
    for (const QVariant &bad:QVariantList{QString("1"),true,0,1001,1.5,std::numeric_limits<double>::quiet_NaN()}) {
        QVariantMap proposal=valid.first().toMap();proposal.insert("pointCount",bad);invalid.append(QVariantList{proposal,valid.last()});
    }
    for (const QString &bad:QStringList{QStringLiteral(""),QString(10001,QLatin1Char('x'))}) {
        QVariantMap proposal=valid.first().toMap();proposal.insert("back",bad);invalid.append(QVariantList{proposal,valid.last()});
    }
    QVariantMap duplicate=valid.last().toMap();duplicate.insert("front","  FIRST   question  ");invalid.append(QVariantList{valid.first(),duplicate});
    for (const QVariantList &proposals:invalid) {
        QVERIFY(!app.replaceCardWithAtomicCards(expected,proposals));QVERIFY(!app.busy());
        QCOMPARE(app.lastError().value("code").toString(),QStringLiteral("INVALID_ATOMIC_SPLIT"));QCOMPARE(app.cards(),original);QCOMPARE(app.pendingCards(),queue);
        QCOMPARE(batch(app).value("events").toList(),originalEvents);
    }
    struct DatabaseAccess db(app.storagePath());QVERIFY(db.run("CREATE TRIGGER fail_atomic_outbox BEFORE INSERT ON sync_outbox WHEN NEW.event_type='card.upsert' BEGIN SELECT RAISE(ABORT,'atomic failure'); END"));
    QSignalSpy split(&app,&AppController::atomicSplitApplied);QVERIFY(app.replaceCardWithAtomicCards(expected,valid));QVERIFY(idle(app));
    QCOMPARE(split.size(),1);QVERIFY(!split.first().at(1).toBool());QVERIFY(split.first().at(2).toStringList().isEmpty());
    QCOMPARE(app.lastError().value("code").toString(),QStringLiteral("ATOMIC_SPLIT_SAVE"));QCOMPARE(app.cards(),original);QCOMPARE(app.pendingCards(),queue);
    QCOMPARE(db.count("tombstones"),0);QCOMPARE(db.count("notes"),1);QCOMPARE(db.count("review_variants"),2);QCOMPARE(batch(app).value("events").toList(),originalEvents);
    QVERIFY(db.run("DROP TRIGGER fail_atomic_outbox"));
    QVERIFY(app.saveCard(sourceId,deckId,"reverse","Edited while proposal was open","Combined answer",{},2));
    QVERIFY(app.replaceCardWithAtomicCards(expected,valid));QVERIFY(idle(app));QCOMPARE(split.size(),2);QVERIFY(!split.last().at(1).toBool());
    QCOMPARE(app.lastError().value("code").toString(),QStringLiteral("ATOMIC_SPLIT_STALE"));QCOMPARE(app.cards().size(),1);QCOMPARE(app.queueCount(),2);
    QCOMPARE(card(app,sourceId).value("front").toString(),QStringLiteral("Edited while proposal was open"));QCOMPARE(db.count("tombstones"),0);
    app.deleteCard(sourceId);QVERIFY(idle(app));const QVariantList before=batch(app).value("events").toList();
    QVERIFY(app.replaceCardWithAtomicCards(expected,valid));QVERIFY(idle(app));QCOMPARE(split.size(),3);QVERIFY(!split.last().at(1).toBool());
    QCOMPARE(app.lastError().value("code").toString(),QStringLiteral("ATOMIC_SPLIT_STALE"));QVERIFY(app.cards().isEmpty());QCOMPARE(batch(app).value("events").toList(),before);
    const QString imageName=QString::fromLatin1(QCryptographicHash::hash("image",QCryptographicHash::Sha256).toHex())+".png";
    struct model::Card withImage=*model::cardFromMap(expected);withImage.front=QStringLiteral("![Source](media:%1)").arg(imageName);
    QVERIFY(!model::atomicCardsFromProposals(withImage,valid));
    QVariantMap withAttachment=valid.first().toMap();withAttachment.insert("back",QStringLiteral("First answer\n![Source](media:%1)").arg(imageName));
    QVERIFY(model::atomicCardsFromProposals(withImage,{withAttachment,valid.last()}));
}

void CoreTest::examplesAreExplicitAndAtomic()
{
    QTemporaryDir directory;AppController app(directory.path());QVERIFY(idle(app));QVERIFY(app.cards().isEmpty());QVERIFY(app.decks().isEmpty());
    {
        struct DatabaseAccess db(app.storagePath());QVERIFY(db.run("CREATE TRIGGER fail_outbox BEFORE INSERT ON sync_outbox BEGIN SELECT RAISE(ABORT,'example failure'); END"));
        app.loadExampleDeck();QVERIFY(idle(app));QCOMPARE(app.lastError().value("code").toString(),QStringLiteral("EXAMPLE_SAVE"));
        app.refresh();QVERIFY(idle(app));QVERIFY(app.cards().isEmpty());QVERIFY(app.decks().isEmpty());QCOMPARE(db.count("sync_outbox"),0);
        QVERIFY(db.run("DROP TRIGGER fail_outbox"));
    }
    app.loadExampleDeck();QVERIFY(idle(app));QCOMPARE(app.decks().size(),1);QCOMPARE(app.cards().size(),7);
    bool code=false,math=false,multipoint=false,cloze=false,reverse=false;
    for (const QVariant &v:app.cards()) {
        const QVariantMap note=v.toMap();code|=note.value("front").toString().contains("```cpp");math|=note.value("back").toString().contains("$$");
        multipoint|=note.value("pointCount").toInt()>1;cloze|=note.value("kind")==QStringLiteral("cloze");reverse|=note.value("kind")==QStringLiteral("reverse");
    }
    QVERIFY(code&&math&&multipoint&&cloze&&reverse);app.startReview();QVERIFY(idle(app));QCOMPARE(app.queueCount(),9);
}

QTEST_GUILESS_MAIN(CoreTest)
#include "core_test.moc"
