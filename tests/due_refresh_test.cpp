#include "core/appcontroller.h"
#include "core/cardmodel.h"

#include <QDateTime>
#include <QElapsedTimer>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QTemporaryDir>
#include <QTest>
#include <QSignalSpy>

namespace model = betterflash::model;

namespace {
bool waitUntilIdle(AppController &app)
{
    QElapsedTimer timer;
    timer.start();
    while (app.busy() && timer.elapsed() < 5000) QTest::qWait(10);
    return !app.busy();
}

QString addedCardId(const AppController &app)
{
    if (app.cards().isEmpty()) return {};
    return app.cards().first().toMap().value("id").toString();
}

int scalar(const QString &databasePath, const QString &sql)
{
    const QString name = model::uuid();
    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), name);
    db.setDatabaseName(databasePath);
    if (!db.open()) return -1;
    int value = -1;
    {
        QSqlQuery query(db);
        if (query.exec(sql) && query.next()) value = query.value(0).toInt();
    }
    db.close();
    db = QSqlDatabase();
    QSqlDatabase::removeDatabase(name);
    return value;
}

QString scheduleState(const QString &databasePath)
{
    const QString name = model::uuid();
    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), name);
    db.setDatabaseName(databasePath);
    if (!db.open()) return {};
    QStringList rows;
    {
        QSqlQuery query(db);
        if (query.exec(QStringLiteral("SELECT * FROM review_variants ORDER BY id"))) {
            const int columns=query.record().count();
            while (query.next()) {
                QStringList values;
                for (int column=0;column<columns;++column) values.append(query.value(column).toString());
                rows.append(values.join(QLatin1Char('|')));
            }
        }
    }
    db.close();
    db = QSqlDatabase();
    QSqlDatabase::removeDatabase(name);
    return rows.join(QLatin1Char('\n'));
}

bool setDue(const QString &databasePath, const QString &cardId, const QDateTime &due)
{
    const QString name = model::uuid();
    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), name);
    db.setDatabaseName(databasePath);
    if (!db.open()) return false;
    bool ok = false;
    {
        QSqlQuery query(db);
        query.prepare(QStringLiteral("UPDATE review_variants SET due=? WHERE card_id=?"));
        query.addBindValue(due.toUTC().toString(Qt::ISODateWithMs));
        query.addBindValue(cardId);
        ok = query.exec() && query.numRowsAffected() > 0;
    }
    db.close();
    db = QSqlDatabase();
    QSqlDatabase::removeDatabase(name);
    return ok;
}

QVariantMap deck(const AppController &app)
{
    if (app.decks().isEmpty()) return {};
    return app.decks().first().toMap();
}
}

class DueRefreshTest final : public QObject
{
    Q_OBJECT
private slots:
    void updatesDueCountWithoutChangingReviewDataOrUserFeedback()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString databasePath = directory.filePath(QStringLiteral("betterflash.sqlite"));
        QString cardId;
        {
            AppController setup(directory.path());
            QVERIFY(waitUntilIdle(setup));
            setup.createDeck(QStringLiteral("Study"));
            QVERIFY(waitUntilIdle(setup));
            QVERIFY(!setup.decks().isEmpty());
            const QString deckId = deck(setup).value("id").toString();
            setup.saveCard({}, deckId, QStringLiteral("reverse"), QStringLiteral("Front"), QStringLiteral("Back"), {});
            QVERIFY(waitUntilIdle(setup));
            cardId = addedCardId(setup);
            QVERIFY(!cardId.isEmpty());
        }
        AppController app(directory.path());
        QVERIFY(waitUntilIdle(app));
        QCOMPARE(deck(app).value("dueCount").toInt(), 2);
        QVERIFY(setDue(databasePath, cardId, QDateTime::currentDateTimeUtc().addMSecs(1500)));
        app.refresh();
        QVERIFY(waitUntilIdle(app));
        QCOMPARE(deck(app).value("dueCount").toInt(), 0);
        const int historiesBefore = scalar(databasePath, QStringLiteral("SELECT COUNT(*) FROM reviews"));
        const int outboxBefore = scalar(databasePath, QStringLiteral("SELECT COUNT(*) FROM sync_outbox"));
        const QString schedulesBefore = scheduleState(databasePath);
        QVERIFY(historiesBefore >= 0);
        QVERIFY(outboxBefore >= 0);
        QVERIFY(!schedulesBefore.isEmpty());

        QSignalSpy cardsContentChanged(&app, &AppController::cardsChanged);
        QSignalSpy decksContentChanged(&app, &AppController::decksChanged);
        QSignalSpy cardsSnapshotChanged(&app, &AppController::cardsSnapshotChanged);
        QSignalSpy decksSnapshotChanged(&app, &AppController::decksSnapshotChanged);
        QSignalSpy dueCountsChanged(&app, &AppController::dueCountsChanged);
        app.createDeck(QStringLiteral("Invalid parent"), {}, model::uuid());
        QVERIFY(waitUntilIdle(app));
        QVERIFY(!app.lastError().isEmpty());
        const QVariantMap expectedError = app.lastError();
        const QString expectedStatus = app.statusMessage();
        QTRY_COMPARE_WITH_TIMEOUT(deck(app).value("dueCount").toInt(), 2, 4000);
        QVERIFY(cardsContentChanged.isEmpty());
        QVERIFY(decksContentChanged.isEmpty());
        QVERIFY(!cardsSnapshotChanged.isEmpty());
        QVERIFY(!decksSnapshotChanged.isEmpty());
        QVERIFY(!dueCountsChanged.isEmpty());
        QCOMPARE(app.lastError(), expectedError);
        QCOMPARE(app.statusMessage(), expectedStatus);
        QCOMPARE(scalar(databasePath, QStringLiteral("SELECT COUNT(*) FROM reviews")), historiesBefore);
        QCOMPARE(scalar(databasePath, QStringLiteral("SELECT COUNT(*) FROM sync_outbox")), outboxBefore);
        QCOMPARE(scalar(databasePath, QStringLiteral("SELECT COUNT(*) FROM review_variants")), 2);
        QCOMPARE(scheduleState(databasePath), schedulesBefore);
    }

    void automaticDueRefreshKeepsAnActiveReviewSessionAndElapsedAnswer()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString databasePath = directory.filePath(QStringLiteral("betterflash.sqlite"));
        QString futureCardId;
        {
            AppController setup(directory.path());
            QVERIFY(waitUntilIdle(setup));
            setup.createDeck(QStringLiteral("Study"));
            QVERIFY(waitUntilIdle(setup));
            const QString deckId = deck(setup).value("id").toString();
            setup.saveCard({}, deckId, QStringLiteral("reverse"), QStringLiteral("Due"), QStringLiteral("Answer"), {});
            QVERIFY(waitUntilIdle(setup));
            setup.saveCard({}, deckId, QStringLiteral("reverse"), QStringLiteral("Later"), QStringLiteral("Answer"), {});
            QVERIFY(waitUntilIdle(setup));
            futureCardId = setup.cards().last().toMap().value("id").toString();
            QVERIFY(!futureCardId.isEmpty());
        }
        QVERIFY(setDue(databasePath, futureCardId, QDateTime::currentDateTimeUtc().addSecs(3600)));

        AppController app(directory.path());
        QVERIFY(waitUntilIdle(app));
        QCOMPARE(deck(app).value("dueCount").toInt(), 2);
        app.startReview(deck(app).value("id").toString());
        QVERIFY(waitUntilIdle(app));
        QVERIFY(app.reviewing());
        const QString currentVariant = app.currentCard().value("variantId").toString();
        QVERIFY(!currentVariant.isEmpty());
        const int sessionSize = app.sessionTotal();
        const int queueSize = app.queueCount();
        app.revealAnswer();
        QVERIFY(app.answerRevealed());
        const double elapsedBefore = app.responseSeconds();
        QVERIFY(setDue(databasePath, futureCardId, QDateTime::currentDateTimeUtc().addSecs(3)));
        app.refresh();
        QVERIFY(waitUntilIdle(app));
        QCOMPARE(deck(app).value("dueCount").toInt(), 2);
        QTRY_COMPARE_WITH_TIMEOUT(deck(app).value("dueCount").toInt(), 4, 5000);
        QCOMPARE(app.currentCard().value("variantId").toString(), currentVariant);
        QCOMPARE(app.sessionTotal(), sessionSize);
        QCOMPARE(app.queueCount(), queueSize);
        QVERIFY(app.answerRevealed());
        QVERIFY(app.responseSeconds() >= elapsedBefore);
    }
};

QTEST_GUILESS_MAIN(DueRefreshTest)
#include "due_refresh_test.moc"
