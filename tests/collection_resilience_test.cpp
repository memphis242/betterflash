#include "core/appcontroller.h"
#include "core/cardmodel.h"
#include "core/databaseworker.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

namespace {
bool idle(AppController &app)
{
    return QTest::qWaitFor([&app] { return !app.busy(); }, 5000);
}
QVariantMap card(const AppController &app, const QString &id)
{
    for (const QVariant &entry : app.cards())
        if (entry.toMap().value("id").toString() == id) return entry.toMap();
    return {};
}
struct Storage {
    const QString connection = betterflash::model::uuid();
    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connection);
    explicit Storage(const QString &path) { db.setDatabaseName(path); }
    ~Storage() { db.close(); db = QSqlDatabase{}; QSqlDatabase::removeDatabase(connection); }
    QByteArray preservedLearning() const {
        QJsonArray tables;
        for (const QString &table : {QStringLiteral("review_variants"), QStringLiteral("reviews")}) {
            QSqlQuery query(db);
            if (!query.exec(QStringLiteral("SELECT * FROM %1 ORDER BY id").arg(table))) return {};
            QJsonArray rows;
            while (query.next()) {
                QJsonArray row;
                for (int column = 0; column < query.record().count(); ++column)
                    row.append(QJsonValue::fromVariant(query.value(column)));
                rows.append(row);
            }
            tables.append(rows);
        }
        return QJsonDocument(tables).toJson(QJsonDocument::Compact);
    }
};
}

class CollectionResilienceTest final : public QObject {
    Q_OBJECT
private slots:
    void invalidSavedReviewGradesDoNotHideCollection_data() {
        QTest::addColumn<QVariant>("grade");
        QTest::newRow("out-of-range") << QVariant(99);
        QTest::newRow("fractional") << QVariant(1.5);
        QTest::newRow("nonnumeric") << QVariant(QStringLiteral("bad"));
    }
    void invalidSavedReviewGradesDoNotHideCollection() {
        QFETCH(QVariant, grade);
        QTemporaryDir directory; QVERIFY(directory.isValid());
        const QString database=QDir(directory.path()).filePath(QStringLiteral("betterflash.sqlite"));
        QString deck, id;
        {
            AppController app(directory.path()); QVERIFY(idle(app));
            app.createDeck(QStringLiteral("History recovery")); QVERIFY(idle(app));
            deck=app.decks().first().toMap().value("id").toString();
            QVERIFY(app.saveCard({},deck,QStringLiteral("basic"),QStringLiteral("Question"),QStringLiteral("Answer"),{},1));
            QVERIFY(idle(app)); id=app.cards().first().toMap().value("id").toString();
            app.startReview(deck); QVERIFY(idle(app)); QVERIFY(app.reviewing());
            app.revealAnswer(); QVERIFY(idle(app)); app.grade(3); QVERIFY(idle(app)); QVERIFY2(!app.history().isEmpty(),qPrintable(QJsonDocument::fromVariant(app.lastError()).toJson()));
        }
        {
            struct Storage storage(database); QVERIFY(storage.db.open());
            QSqlQuery constraints(storage.db); QVERIFY(constraints.exec(QStringLiteral("PRAGMA ignore_check_constraints=ON")));
            QSqlQuery corrupt(storage.db); QVERIFY(corrupt.prepare(QStringLiteral("UPDATE reviews SET grade=? WHERE card_id=?")));
            corrupt.addBindValue(grade); corrupt.addBindValue(id); QVERIFY(corrupt.exec());
        }
        AppController reopened(directory.path()); QVERIFY(idle(reopened));
        QVERIFY(reopened.collectionLoaded()); QCOMPARE(reopened.decks().size(),1); QCOMPARE(reopened.cards().size(),1);
        QVERIFY(!reopened.collectionIssues().isEmpty());
        { struct Storage storage(database); QVERIFY(storage.db.open()); QSqlQuery raw(storage.db);
          QVERIFY(raw.prepare(QStringLiteral("SELECT grade FROM reviews WHERE card_id=?"))); raw.addBindValue(id);
          QVERIFY(raw.exec()); QVERIFY(raw.next()); QCOMPARE(raw.value(0),grade); }
    }
    void malformedPendingSyncEventDoesNotHideCollection() {
        QTemporaryDir directory; QVERIFY(directory.isValid());
        const QString database=QDir(directory.path()).filePath(QStringLiteral("betterflash.sqlite"));
        QString deck, id;
        {
            AppController app(directory.path()); QVERIFY(idle(app));
            app.createDeck(QStringLiteral("Outbox recovery")); QVERIFY(idle(app));
            deck=app.decks().first().toMap().value("id").toString();
            QVERIFY(app.saveCard({},deck,QStringLiteral("basic"),QStringLiteral("Question"),QStringLiteral("Answer"),{},1));
            QVERIFY(idle(app)); id=app.cards().first().toMap().value("id").toString();
        }
        {
            struct Storage storage(database); QVERIFY(storage.db.open()); QSqlQuery corrupt(storage.db);
            QVERIFY(corrupt.prepare(QStringLiteral("UPDATE sync_outbox SET payload=? WHERE target_id=?")));
            corrupt.addBindValue(QStringLiteral("not-json")); corrupt.addBindValue(id); QVERIFY(corrupt.exec());
        }
        AppController reopened(directory.path()); QVERIFY(idle(reopened));
        QVERIFY(reopened.collectionLoaded()); QCOMPARE(reopened.decks().size(),1); QCOMPARE(reopened.cards().size(),1);
        QVERIFY(!reopened.collectionIssues().isEmpty());
        reopened.requestSyncBatch(); QVERIFY(idle(reopened)); QVERIFY(!reopened.lastError().isEmpty());
        { struct Storage storage(database); QVERIFY(storage.db.open()); QSqlQuery raw(storage.db);
          QVERIFY(raw.prepare(QStringLiteral("SELECT payload FROM sync_outbox WHERE target_id=?"))); raw.addBindValue(id);
          QVERIFY(raw.exec()); QVERIFY(raw.next()); QCOMPARE(raw.value(0).toString(),QStringLiteral("not-json")); }
    }
    void inconsistentReviewSessionMetadataIsPreservedAndBlocksExport() {
        QTemporaryDir directory; QVERIFY(directory.isValid());
        const QString database=QDir(directory.path()).filePath(QStringLiteral("betterflash.sqlite"));
        QString deck, id;
        {
            AppController app(directory.path()); QVERIFY(idle(app));
            app.createDeck(QStringLiteral("History metadata")); QVERIFY(idle(app));
            deck=app.decks().first().toMap().value("id").toString();
            QVERIFY(app.saveCard({},deck,QStringLiteral("basic"),QStringLiteral("Question"),QStringLiteral("Answer"),{},1));
            QVERIFY(idle(app)); id=app.cards().first().toMap().value("id").toString();
            app.startReview(deck); QVERIFY(idle(app)); QVERIFY(app.reviewing()); app.revealAnswer(); QVERIFY(idle(app));
            app.grade(3); QVERIFY(idle(app)); QVERIFY(!app.history().isEmpty());
        }
        {
            struct Storage storage(database); QVERIFY(storage.db.open()); QSqlQuery corrupt(storage.db);
            QVERIFY(corrupt.prepare(QStringLiteral("UPDATE reviews SET session_id='',session_deck_name='orphaned' WHERE card_id=?")));
            corrupt.addBindValue(id); QVERIFY(corrupt.exec());
        }
        AppController reopened(directory.path()); QVERIFY(idle(reopened));
        QVERIFY(reopened.collectionLoaded()); QCOMPARE(reopened.cards().size(),1); QVERIFY(!reopened.collectionIssues().isEmpty());
        const QString output=QDir(directory.path()).filePath(QStringLiteral("portable.json"));
        reopened.exportCollection(output); QVERIFY(idle(reopened)); QVERIFY(!reopened.lastError().isEmpty()); QVERIFY(!QFile::exists(output));
        { struct Storage storage(database); QVERIFY(storage.db.open()); QSqlQuery raw(storage.db);
          QVERIFY(raw.prepare(QStringLiteral("SELECT session_id,session_deck_name FROM reviews WHERE card_id=?"))); raw.addBindValue(id);
          QVERIFY(raw.exec()); QVERIFY(raw.next()); QCOMPARE(raw.value(0).toString(),QString{}); QCOMPARE(raw.value(1).toString(),QStringLiteral("orphaned")); }
    }
    void schemaSevenOrphanScheduleDoesNotHideCollection() {
        QTemporaryDir directory; QVERIFY(directory.isValid());
        const QString database=QDir(directory.path()).filePath(QStringLiteral("betterflash.sqlite"));
        QString deck, id, orphanCard;
        {
            AppController app(directory.path()); QVERIFY(idle(app));
            app.createDeck(QStringLiteral("Schedule migration")); QVERIFY(idle(app));
            deck=app.decks().first().toMap().value("id").toString();
            QVERIFY(app.saveCard({},deck,QStringLiteral("basic"),QStringLiteral("Question"),QStringLiteral("Answer"),{},1));
            QVERIFY(idle(app)); id=app.cards().first().toMap().value("id").toString();
        }
        orphanCard=betterflash::model::uuid();
        QVariantMap orphan;
        {
            struct Storage storage(database); QVERIFY(storage.db.open());
            QSqlQuery pragmas(storage.db);
            QVERIFY(pragmas.exec(QStringLiteral("PRAGMA foreign_keys=OFF")));
            QVERIFY(pragmas.exec(QStringLiteral("PRAGMA ignore_check_constraints=ON")));
            QSqlQuery source(storage.db); QVERIFY(source.prepare(QStringLiteral("SELECT id,variant_key,due,review_count,stability,difficulty,graduated FROM review_variants WHERE card_id=?")));
            source.addBindValue(id); QVERIFY(source.exec()); QVERIFY(source.next());
            orphan={{"id",betterflash::model::variantId(orphanCard,source.value(1).toString())},{"cardId",orphanCard},{"variantKey",source.value(1)},
                    {"due",source.value(2)},{"reviewCount",source.value(3)},{"stability",source.value(4)},{"difficulty",source.value(5)},{"graduated",source.value(6)}};
            QSqlQuery insert(storage.db); QVERIFY(insert.prepare(QStringLiteral("INSERT INTO review_variants(id,card_id,variant_key,due,review_count,stability,difficulty,graduated) VALUES(?,?,?,?,?,?,?,?)")));
            for (const QString &key:{QStringLiteral("id"),QStringLiteral("cardId"),QStringLiteral("variantKey"),QStringLiteral("due"),QStringLiteral("reviewCount"),QStringLiteral("stability"),QStringLiteral("difficulty"),QStringLiteral("graduated")}) insert.addBindValue(orphan.value(key));
            QVERIFY(insert.exec()); QVERIFY(pragmas.exec(QStringLiteral("PRAGMA user_version=7")));
        }
        AppController reopened(directory.path()); QVERIFY(idle(reopened));
        QVERIFY(reopened.collectionLoaded()); QCOMPARE(reopened.decks().size(),1); QCOMPARE(reopened.cards().size(),1);
        QVERIFY(!reopened.collectionIssues().isEmpty());
        bool found=false; for (const QVariant &value:reopened.collectionIssues()) found|=value.toMap().value("code").toString()==QStringLiteral("ORPHAN_SCHEDULE"); QVERIFY(found);
        const QStringList backups=QDir(QDir(directory.path()).filePath(QStringLiteral("recovery"))).entryList({QStringLiteral("*.sqlite")},QDir::Files); QVERIFY(!backups.isEmpty());
        struct Storage storage(database); QVERIFY(storage.db.open()); QSqlQuery raw(storage.db);
        QVERIFY(raw.prepare(QStringLiteral("SELECT id,card_id,variant_key,due,review_count,stability,difficulty,graduated FROM review_variants WHERE card_id=?")));
        raw.addBindValue(orphanCard); QVERIFY(raw.exec()); QVERIFY(raw.next());
        QCOMPARE(raw.value(0).toString(),orphan.value("id").toString()); QCOMPARE(raw.value(2).toString(),orphan.value("variantKey").toString());
    }
    void unreadableCardDoesNotHideCollectionOrBlockHealthyReview_data() {
        QTest::addColumn<QString>("field"); QTest::addColumn<QString>("invalidValue");
        QTest::newRow("unfinished-cloze") << QStringLiteral("front") << QStringLiteral("A [cloze1:unfinished");
        QTest::newRow("unsupported-kind") << QStringLiteral("kind") << QStringLiteral("future-kind");
        QTest::newRow("invalid-narration-json") << QStringLiteral("narrations") << QStringLiteral("not a JSON object");
        QTest::newRow("invalid-narration-object") << QStringLiteral("narrations") << QStringLiteral("{\"c1/front\":\"invalid.wav\"}");
        QTest::newRow("invalid-fingerprint-object") << QStringLiteral("narration_fingerprints") << QStringLiteral("{\"c1/front\":\"invalid\"}");
        QTest::newRow("fractional-answer-points") << QStringLiteral("point_count") << QStringLiteral("1.5");
        QTest::newRow("blob-answer-points") << QStringLiteral("point_count") << QStringLiteral("1");
    }
    void unreadableCardDoesNotHideCollectionOrBlockHealthyReview() {
        QFETCH(QString, field); QFETCH(QString, invalidValue);
        QTemporaryDir directory; QVERIFY(directory.isValid());
        const QString database = QDir(directory.path()).filePath(QStringLiteral("betterflash.sqlite"));
        QString deck, healthy, broken;
        {
            AppController app(directory.path()); QVERIFY(idle(app));
            app.createDeck(QStringLiteral("Saved study material")); QVERIFY(idle(app));
            deck = app.decks().first().toMap().value("id").toString();
            QVERIFY(app.saveCard({}, deck, QStringLiteral("basic"), QStringLiteral("Healthy question"), QStringLiteral("Healthy answer"), {}, 1));
            QVERIFY(idle(app)); healthy = app.cards().first().toMap().value("id").toString();
            QVERIFY(app.saveCard({}, deck, QStringLiteral("cloze"), QStringLiteral("A [cloze1:preserved answer]."), {}, {}, 1));
            QVERIFY(idle(app));
            for (const QVariant &entry : app.cards())
                if (entry.toMap().value("id").toString() != healthy) broken = entry.toMap().value("id").toString();
        }
        QByteArray learning;
        {
            struct Storage storage(database); QVERIFY(storage.db.open());
            QSqlQuery constraints(storage.db); QVERIFY(constraints.exec(QStringLiteral("PRAGMA ignore_check_constraints=ON")));
            QSqlQuery corrupt(storage.db);
            QVERIFY(corrupt.prepare(QStringLiteral("UPDATE notes SET %1=? WHERE id=?").arg(field)));
            corrupt.addBindValue(field==QStringLiteral("point_count")&&invalidValue==QStringLiteral("1")?QVariant(QByteArray("1")):QVariant(invalidValue)); corrupt.addBindValue(broken);
            QVERIFY(corrupt.exec()); learning = storage.preservedLearning(); QVERIFY(!learning.isEmpty());
        }
        {
            AppController app(directory.path()); QVERIFY(idle(app));
            QCOMPARE(app.decks().size(), 1);
            QCOMPARE(app.cards().size(), 2);
            QCOMPARE(card(app, broken).value("front").toString(), field==QStringLiteral("front")?invalidValue:QStringLiteral("A [cloze1:preserved answer]."));
            QVERIFY(!card(app, broken).value("loadError").toMap().isEmpty());
            QVERIFY(card(app, healthy).value("loadError").toMap().isEmpty());
            { struct Storage storage(database); QVERIFY(storage.db.open()); QCOMPARE(storage.preservedLearning(), learning); }
            const QString exportPath=QDir(directory.path()).filePath(QStringLiteral("damaged-export.json"));
            app.exportCollection(exportPath); QVERIFY(idle(app));
            QVERIFY(!app.lastError().isEmpty()); QVERIFY(!QFile::exists(exportPath));
            app.startReview(deck); QVERIFY(idle(app));
            QVERIFY(app.reviewing()); QCOMPARE(app.queueCount(), 1);
            QCOMPARE(app.currentCard().value("cardId", app.currentCard().value("id")).toString(), healthy);
            app.stopReview(); QVERIFY(idle(app));
            QVERIFY(app.saveCard(broken, deck, QStringLiteral("cloze"), QStringLiteral("A [cloze1:repaired answer]."), {}, {}, 1));
            QVERIFY(idle(app));
            QVERIFY2(app.lastError().isEmpty(), qPrintable(QJsonDocument::fromVariant(app.lastError()).toJson()));
            QVERIFY(card(app, broken).value("loadError").toMap().isEmpty());
            const QStringList backups=QDir(QDir(directory.path()).filePath(QStringLiteral("recovery"))).entryList({QStringLiteral("*.sqlite")},QDir::Files);
            QCOMPARE(backups.size(),1);
            {
                struct Storage recovery(QDir(directory.path()).filePath(QStringLiteral("recovery/")+backups.first()));
                QVERIFY(recovery.db.open()); QCOMPARE(recovery.preservedLearning(),learning);
                QSqlQuery original(recovery.db);
                QVERIFY(original.prepare(QStringLiteral("SELECT %1 FROM notes WHERE id=?").arg(field)));
                original.addBindValue(broken); QVERIFY(original.exec()); QVERIFY(original.next()); QCOMPARE(original.value(0).toString(),invalidValue);
            }
            { struct Storage storage(database); QVERIFY(storage.db.open()); QCOMPARE(storage.preservedLearning(), learning); }
            QVERIFY(app.saveCard(healthy, deck, QStringLiteral("basic"), QStringLiteral("Healthy edited question"), QStringLiteral("Healthy answer"), {}, 1));
            QVERIFY(idle(app)); QVERIFY(app.lastError().isEmpty());
        }
        AppController restarted(directory.path()); QVERIFY(idle(restarted));
        QCOMPARE(restarted.cards().size(), 2); QCOMPARE(restarted.decks().size(), 1);
        QVERIFY(card(restarted, broken).value("loadError").toMap().isEmpty());
    }
    void damagedScheduleIsIsolatedAndResetPreservesOriginal_data() {
        QTest::addColumn<QString>("field"); QTest::addColumn<QVariant>("invalid");
        QTest::addColumn<bool>("resetAllowed"); QTest::addColumn<bool>("olderSchema");
        QTest::newRow("fractional-count") << QStringLiteral("review_count") << QVariant(1.5) << true << false;
        QTest::newRow("bad-stability") << QStringLiteral("stability") << QVariant(QStringLiteral("bad")) << true << false;
        QTest::newRow("bad-phase") << QStringLiteral("graduated") << QVariant(2) << true << false;
        QTest::newRow("bad-direction") << QStringLiteral("variant_key") << QVariant(QStringLiteral("sideways")) << false << false;
        QTest::newRow("old-schema-bad-stability") << QStringLiteral("stability") << QVariant(-2.0) << true << true;
    }
    void damagedScheduleIsIsolatedAndResetPreservesOriginal() {
        QFETCH(QString,field); QFETCH(QVariant,invalid); QFETCH(bool,resetAllowed); QFETCH(bool,olderSchema);
        QTemporaryDir directory; QVERIFY(directory.isValid());
        const QString database=QDir(directory.path()).filePath(QStringLiteral("betterflash.sqlite"));
        QString deck,broken,healthy;
        {
            AppController app(directory.path()); QVERIFY(idle(app)); app.createDeck(QStringLiteral("Study")); QVERIFY(idle(app));
            deck=app.decks().first().toMap().value("id").toString();
            QVERIFY(app.saveCard({},deck,QStringLiteral("basic"),QStringLiteral("Broken schedule question"),QStringLiteral("Answer"),{},1)); QVERIFY(idle(app));
            broken=app.cards().first().toMap().value("id").toString();
            QVERIFY(app.saveCard({},deck,QStringLiteral("basic"),QStringLiteral("Healthy question"),QStringLiteral("Answer"),{},1)); QVERIFY(idle(app));
            for (const QVariant &entry:app.cards()) if (entry.toMap().value("id").toString()!=broken) healthy=entry.toMap().value("id").toString();
        }
        QByteArray learning;
        {
            struct Storage storage(database); QVERIFY(storage.db.open()); QSqlQuery query(storage.db);
            QVERIFY(query.exec(QStringLiteral("PRAGMA ignore_check_constraints=ON")));
            QVERIFY(query.prepare(QStringLiteral("UPDATE review_variants SET %1=? WHERE card_id=?").arg(field)));
            query.addBindValue(invalid);query.addBindValue(broken);QVERIFY(query.exec());
            if (olderSchema) QVERIFY(query.exec(QStringLiteral("PRAGMA user_version=7")));
            learning=storage.preservedLearning();QVERIFY(!learning.isEmpty());
        }
        AppController app(directory.path());QVERIFY(idle(app));
        QVERIFY2(app.collectionLoaded(),qPrintable(QJsonDocument::fromVariant(app.lastError()).toJson()));
        QCOMPARE(app.cards().size(),2);QCOMPARE(app.decks().size(),1);
        QVERIFY(!card(app,broken).value("loadError").toMap().isEmpty());
        QVERIFY(card(app,healthy).value("loadError").toMap().isEmpty());
        app.startReview(deck);QVERIFY(idle(app));QCOMPARE(app.queueCount(),1);app.stopReview();QVERIFY(idle(app));
        const QString exportPath=QDir(directory.path()).filePath(QStringLiteral("unsafe-export.json"));
        app.exportCollection(exportPath);QVERIFY(idle(app));QVERIFY(!app.lastError().isEmpty());QVERIFY(!QFile::exists(exportPath));
        {struct Storage storage(database);QVERIFY(storage.db.open());QCOMPARE(storage.preservedLearning(),learning);}
        app.resetCard(broken);QVERIFY(idle(app));
        if (!resetAllowed) {
            QVERIFY(!app.lastError().isEmpty());
            struct Storage storage(database);QVERIFY(storage.db.open());QCOMPARE(storage.preservedLearning(),learning);return;
        }
        QVERIFY2(app.lastError().isEmpty(),qPrintable(QJsonDocument::fromVariant(app.lastError()).toJson()));
        QVERIFY(card(app,broken).value("loadError").toMap().isEmpty());
        const QDir recovery(QDir(directory.path()).filePath(QStringLiteral("recovery")));
        const QStringList backups=recovery.entryList({QStringLiteral("*.sqlite")},QDir::Files);
        QVERIFY(!backups.isEmpty());bool exactOriginal=false;
        for (const QString &name:backups) {
            struct Storage saved(recovery.filePath(name));QVERIFY(saved.db.open());
            if (saved.preservedLearning()==learning) exactOriginal=true;
        }
        QVERIFY(exactOriginal);
    }
    void legacyInvalidScheduleAndHistoryDoNotHideHealthyCollection() {
        QTemporaryDir directory;QVERIFY(directory.isValid());
        const QString database=QDir(directory.path()).filePath(QStringLiteral("betterflash.sqlite"));
        QString deck;const QString broken=betterflash::model::uuid();
        {
            AppController app(directory.path());QVERIFY(idle(app));app.createDeck(QStringLiteral("Legacy collection"));QVERIFY(idle(app));
            deck=app.decks().first().toMap().value("id").toString();
            QVERIFY(app.saveCard({},deck,QStringLiteral("basic"),QStringLiteral("Healthy question"),QStringLiteral("Answer"),{},1));QVERIFY(idle(app));
        }
        {
            struct Storage storage(database);QVERIFY(storage.db.open());QSqlQuery query(storage.db);
            QVERIFY(query.exec(QStringLiteral("CREATE TABLE cards(id TEXT,deck_id TEXT,kind TEXT,front TEXT,back TEXT,tags TEXT,point_count INTEGER,due TEXT,review_count INTEGER,stability REAL,difficulty REAL)")));
            QVERIFY(query.prepare(QStringLiteral("INSERT INTO cards VALUES(?,?,'basic','Retained question','Retained answer','',1,'2020-01-01',-1,1,0.5)")));
            query.addBindValue(broken);query.addBindValue(deck);QVERIFY(query.exec());
            QVERIFY(query.exec(QStringLiteral("CREATE TABLE history(id TEXT,card_id TEXT,deck_name TEXT,grade TEXT,recall_fraction REAL,response_seconds REAL,reviewed_at TEXT,due TEXT)")));
            QVERIFY(query.prepare(QStringLiteral("INSERT INTO history VALUES(?,?,'Legacy collection','Unknown',1,1,'2020-01-01T00:00:00.000Z','2020-01-02')")));
            query.addBindValue(betterflash::model::uuid());query.addBindValue(broken);QVERIFY(query.exec());
            QVERIFY(query.exec(QStringLiteral("PRAGMA user_version=1")));
        }
        AppController app(directory.path());QVERIFY(idle(app));
        QVERIFY2(app.collectionLoaded(),qPrintable(QJsonDocument::fromVariant(app.lastError()).toJson()));
        QCOMPARE(app.cards().size(),2);QCOMPARE(app.decks().size(),1);
        QVERIFY(!card(app,broken).value("loadError").toMap().isEmpty());
        app.startReview(deck);QVERIFY(idle(app));QCOMPARE(app.queueCount(),1);
        struct Storage storage(database);QVERIFY(storage.db.open());QSqlQuery query(storage.db);
        QVERIFY(query.exec(QStringLiteral("SELECT review_count FROM cards")));QVERIFY(query.next());QCOMPARE(query.value(0).toInt(),-1);
        QVERIFY(query.exec(QStringLiteral("SELECT grade FROM history")));QVERIFY(query.next());QCOMPARE(query.value(0).toString(),QStringLiteral("Unknown"));
    }
    void derivedGuidanceFailureDoesNotBlockLoadingOrGrading() {
        QTemporaryDir directory; QVERIFY(directory.isValid());
        DatabaseWorker worker(QDir(directory.path()).filePath(QStringLiteral("betterflash.sqlite")));
        const QScopeGuard close([&worker] {worker.shutdown();});
        QSignalSpy snapshots(&worker,&DatabaseWorker::snapshotReady);
        QSignalSpy issues(&worker,&DatabaseWorker::collectionIssuesReady);
        QSignalSpy completed(&worker,&DatabaseWorker::operationFinished);
        worker.initialize(); worker.createDeck(QStringLiteral("Available study cards"),{},{});
        QVERIFY(!snapshots.isEmpty());
        const QString deck=snapshots.last().at(0).toList().first().toMap().value("id").toString();
        worker.saveCard({},deck,QStringLiteral("basic"),QStringLiteral("Question"),QStringLiteral("Answer"),{},1);
        const QString id=snapshots.last().at(1).toList().first().toMap().value("id").toString();
        {
            bool found=false;
            for (const QString &name:QSqlDatabase::connectionNames()) {
                QSqlDatabase db=QSqlDatabase::database(name,false);
                if (db.databaseName()!=QDir(directory.path()).filePath(QStringLiteral("betterflash.sqlite"))) continue;
                QSqlQuery removeDerivedIndex(db); QVERIFY(removeDerivedIndex.exec(QStringLiteral("DROP INDEX reviews_variant_recent")));
                QVERIFY(removeDerivedIndex.exec(QStringLiteral("INSERT OR IGNORE INTO temp.betterflash_study_dirty_variants(id) SELECT id FROM review_variants"))); found=true;break;
            }
            QVERIFY(found);
        }
        worker.snapshot();
        QCOMPARE(snapshots.last().at(0).toList().size(),1);
        QCOMPARE(snapshots.last().at(1).toList().size(),1);
        QVERIFY(snapshots.last().at(1).toList().first().toMap().value("loadError").toMap().isEmpty());
        QVERIFY(!issues.last().at(0).toList().isEmpty());
        QVERIFY(completed.last().at(0).toBool());
        worker.beginReview(deck);
        worker.gradeCard(3,1.0,2.0,betterflash::model::variantId(id,QStringLiteral("forward")));
        QVERIFY(completed.last().at(0).toBool());
        QCOMPARE(snapshots.last().at(1).toList().first().toMap().value("reviewCount").toInt(),1);
    }
    void failedCollectionLoadIsDistinctFromEmptyAndCanRetry() {
        QTemporaryDir original; QTemporaryDir target; QVERIFY(original.isValid()); QVERIFY(target.isValid());
        {
            AppController app(original.path()); QVERIFY(idle(app));
            app.createDeck(QStringLiteral("Retained deck")); QVERIFY(idle(app));
            QVERIFY(app.saveCard({},app.decks().first().toMap().value("id").toString(),QStringLiteral("basic"),QStringLiteral("Saved question"),QStringLiteral("Saved answer"),{},1));
            QVERIFY(idle(app));
        }
        const QString path=QDir(target.path()).filePath(QStringLiteral("betterflash.sqlite"));
        {QFile bad(path); QVERIFY(bad.open(QIODevice::WriteOnly)); QVERIFY(bad.write("unreadable SQLite header")>0);}
        AppController app(target.path()); QVERIFY(idle(app));
        QVERIFY(!app.collectionLoaded()); QVERIFY(!app.lastError().isEmpty());
        QVERIFY(QFile::remove(path));
        QVERIFY(QFile::copy(QDir(original.path()).filePath(QStringLiteral("betterflash.sqlite")),path));
        app.refresh(); QVERIFY(idle(app));
        QVERIFY2(app.collectionLoaded(),qPrintable(QJsonDocument::fromVariant(app.lastError()).toJson()));
        QVERIFY(app.lastError().isEmpty()); QCOMPARE(app.decks().size(),1); QCOMPARE(app.cards().size(),1);
    }
};

QTEST_MAIN(CollectionResilienceTest)
#include "collection_resilience_test.moc"
