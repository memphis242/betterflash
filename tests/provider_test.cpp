#include "core/appcontroller.h"
#include "integrations/Atomicizer.h"
#include "integrations/SecretStore.h"
#include "integrations/Summarizer.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QRegularExpression>
#include <QSettings>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTimer>
#include <QtTest>

class MockChat final : public QTcpServer {
public:
    explicit MockChat(QObject *parent = nullptr) : QTcpServer(parent) {
        connect(this, &QTcpServer::newConnection, this, [this] {
            while (hasPendingConnections()) {
                QTcpSocket *const socket = nextPendingConnection();
                socket->setParent(this);
                connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
                connect(socket, &QTcpSocket::readyRead, socket, [this, socket] {
                    QByteArray incoming = socket->property("incoming").toByteArray() + socket->readAll();
                    socket->setProperty("incoming", incoming);
                    if (socket->property("responded").toBool()) return;
                    const qsizetype boundary = incoming.indexOf("\r\n\r\n");
                    if (boundary < 0) return;
                    const QByteArray headers = incoming.left(boundary);
                    const auto match = QRegularExpression(QStringLiteral("content-length: ([0-9]+)"),
                        QRegularExpression::CaseInsensitiveOption).match(QString::fromLatin1(headers));
                    if (!match.hasMatch() || incoming.size() < boundary + 4 + match.captured(1).toLongLong()) return;
                    socket->setProperty("responded", true);
                    requestHeaders = headers;
                    requestBody = incoming.mid(boundary + 4, match.captured(1).toLongLong());
                    ++requestCount;
                    const QByteArray response = "HTTP/1.1 " + QByteArray::number(status) + " Mock\r\nContent-Type: application/json\r\n"
                        + extraHeaders + "Content-Length: " + QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body;
                    const QPointer<QTcpSocket> guarded(socket);
                    QTimer::singleShot(delayMs, this, [guarded, response] {
                        if (!guarded || guarded->state() != QAbstractSocket::ConnectedState) return;
                        guarded->write(response);
                        guarded->disconnectFromHost();
                    });
                });
            }
        });
    }
    QString endpoint() const { return QStringLiteral("http://127.0.0.1:%1/chat/completions").arg(serverPort()); }
    QByteArray body = R"({"choices":[{"message":{"content":"## Topics\nRemaining concepts."}}]})";
    QByteArray extraHeaders;
    QByteArray requestHeaders;
    QByteArray requestBody;
    int status = 200;
    int delayMs = 0;
    int requestCount = 0;
};

class ProviderTest final : public QObject {
    Q_OBJECT
private:
    QTemporaryDir m_temporary;
    QByteArray m_originalPath;
    QByteArray m_originalLlmKey;
    bool settle(AppController &app) {
        return QTest::qWaitFor([&app] { return !app.busy(); }, 10000) && app.lastError().isEmpty();
    }
    bool seed(AppController &app, int count = 2) {
        if (!settle(app)) return false;
        app.createDeck(QStringLiteral("Provider fixture"));
        if (!settle(app)) return false;
        const QString deck = app.decks().first().toMap().value(QStringLiteral("id")).toString();
        for (int index = 0; index < count; ++index) {
            if (!app.saveCard({}, deck, QStringLiteral("basic"), QStringLiteral("Question %1 about a concept").arg(index),
                    QStringLiteral("Private solution %1").arg(index), {}, 1) || !settle(app)) return false;
        }
        app.startReview(deck);
        return settle(app);
    }
    QByteArray atomicResponse(const QString &decision, const QJsonArray &cards = {}) {
        const QJsonObject proposal{{QStringLiteral("decision"), decision},
            {QStringLiteral("reason"), QStringLiteral("These are independent concepts, or a group that belongs together.")},
            {QStringLiteral("cards"), cards}};
        const QJsonObject message{{QStringLiteral("content"),
            QString::fromUtf8(QJsonDocument(proposal).toJson(QJsonDocument::Compact))}};
        const QJsonArray choices{QJsonObject{{QStringLiteral("message"), message}}};
        return QJsonDocument(QJsonObject{{QStringLiteral("choices"), choices}}).toJson(QJsonDocument::Compact);
    }
    QJsonArray atomicCards(int count = 2) {
        QJsonArray cards;
        for (int index = 0; index < count; ++index)
            cards.append(QJsonObject{{QStringLiteral("front"), QStringLiteral("Targeted question %1").arg(index)},
                {QStringLiteral("back"), QStringLiteral("Complete answer %1").arg(index)}, {QStringLiteral("pointCount"), 1}});
        return cards;
    }
private slots:
    void initTestCase() {
        QVERIFY(m_temporary.isValid());
        QCoreApplication::setOrganizationName(QStringLiteral("BetterFlashTest"));
        QCoreApplication::setApplicationName(QStringLiteral("Provider"));
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_temporary.filePath(QStringLiteral("settings")));
        m_originalPath = qgetenv("PATH");
        m_originalLlmKey = qgetenv("BETTERFLASH_LLM_API_KEY");
        qputenv("BETTERFLASH_LLM_API_KEY", "mock-provider-key");
    }
    void summaryIncludesOnlyTheRemainingQuestions() {
        AppController app(m_temporary.filePath(QStringLiteral("summary")));
        QVERIFY(seed(app, 3));
        app.revealAnswer(); app.grade(3); QVERIFY(settle(app));
        QCOMPARE(app.pendingCards().size(), 2);
        MockChat provider; QVERIFY(provider.listen(QHostAddress::LocalHost, 0));
        Summarizer summary(&app); summary.setEndpoint(provider.endpoint()); summary.setModel(QStringLiteral("mock-model"));
        summary.summarizeRemaining();
        QVERIFY(QTest::qWaitFor([&summary] { return !summary.busy(); }, 5000));
        QVERIFY2(summary.error().isEmpty(), qPrintable(summary.error()));
        QVERIFY(!summary.summary().isEmpty());
        QCOMPARE(summary.pendingCount(), 2);
        QVERIFY(provider.requestHeaders.contains("Bearer mock-provider-key"));
        const QJsonObject request = QJsonDocument::fromJson(provider.requestBody).object();
        QCOMPARE(request.value(QStringLiteral("model")).toString(), QStringLiteral("mock-model"));
        const QString prompts = request.value(QStringLiteral("messages")).toArray().last().toObject().value(QStringLiteral("content")).toString();
        QVERIFY(prompts.contains(QStringLiteral("remaining review items: 2")));
        QVERIFY(!prompts.contains(QStringLiteral("Private solution")));
        QVERIFY(!provider.requestBody.contains("mock-provider-key"));
    }
    void providerFailures_data() {
        QTest::addColumn<int>("status");
        QTest::addColumn<QByteArray>("body");
        QTest::addColumn<QString>("code");
        QTest::newRow("authentication") << 401 << QByteArray("{}") << QStringLiteral("LLM_HTTP_401");
        QTest::newRow("rate-limit") << 429 << QByteArray("{}") << QStringLiteral("LLM_HTTP_429");
        QTest::newRow("invalid-json") << 200 << QByteArray("{broken") << QStringLiteral("LLM_RESPONSE");
        QTest::newRow("empty-result") << 200 << QByteArray("{\"choices\":[]}") << QStringLiteral("LLM_RESPONSE");
        QTest::newRow("redirect") << 302 << QByteArray("{}") << QStringLiteral("LLM_HTTP_302");
        QTest::newRow("large-response") << 200 << QByteArray(600 * 1024, 'x') << QStringLiteral("LLM_");
    }
    void providerFailures() {
        QFETCH(int, status); QFETCH(QByteArray, body); QFETCH(QString, code);
        QTemporaryDir directory; QVERIFY(directory.isValid());
        AppController app(directory.path()); QVERIFY(seed(app));
        MockChat redirectTarget; QVERIFY(redirectTarget.listen(QHostAddress::LocalHost, 0));
        MockChat provider; QVERIFY(provider.listen(QHostAddress::LocalHost, 0));
        provider.status = status; provider.body = body;
        provider.extraHeaders = "Location: " + redirectTarget.endpoint().toUtf8() + "\r\n";
        Summarizer summary(&app); summary.setEndpoint(provider.endpoint()); summary.setModel(QStringLiteral("mock-model"));
        summary.summarizeRemaining();
        QVERIFY(QTest::qWaitFor([&summary] { return !summary.busy(); }, 5000));
        QVERIFY2(summary.error().startsWith(code), qPrintable(summary.error()));
        QVERIFY(summary.summary().isEmpty());
        QCOMPARE(redirectTarget.requestCount, 0);
    }
    void canceledSummaryCannotReplaceASecondRequest() {
        AppController app(m_temporary.filePath(QStringLiteral("cancel"))); QVERIFY(seed(app));
        MockChat provider; QVERIFY(provider.listen(QHostAddress::LocalHost, 0));
        provider.delayMs = 300;
        provider.body = R"({"choices":[{"message":{"content":"obsolete"}}]})";
        Summarizer summary(&app); summary.setEndpoint(provider.endpoint()); summary.setModel(QStringLiteral("mock-model"));
        summary.summarizeRemaining();
        QVERIFY(QTest::qWaitFor([&provider] { return provider.requestCount == 1; }, 5000));
        summary.cancel(); QVERIFY(!summary.busy());
        provider.delayMs = 0;
        provider.body = R"({"choices":[{"message":{"content":"current"}}]})";
        summary.summarizeRemaining();
        QVERIFY(QTest::qWaitFor([&summary] { return !summary.busy(); }, 5000));
        QCOMPARE(summary.summary(), QStringLiteral("current"));
        QTest::qWait(400);
        QCOMPARE(summary.summary(), QStringLiteral("current"));
    }
    void unsafeEndpointsDoNotSendCredentials() {
        AppController app(m_temporary.filePath(QStringLiteral("endpoint"))); QVERIFY(seed(app));
        Summarizer summary(&app);
        summary.setEndpoint(QStringLiteral("http://example.invalid/chat/completions"));
        summary.summarizeRemaining();
        QVERIFY(!summary.busy());
        QVERIFY(summary.error().startsWith(QStringLiteral("LLM_ENDPOINT")));
    }
    void synchronousNotificationCancellationStopsTheRequest() {
        AppController app(m_temporary.filePath(QStringLiteral("notification-cancel"))); QVERIFY(seed(app));
        MockChat provider; QVERIFY(provider.listen(QHostAddress::LocalHost, 0));
        provider.delayMs = 300;
        provider.body = atomicResponse(QStringLiteral("split"), atomicCards());
        Summarizer summary(&app); summary.setEndpoint(provider.endpoint()); summary.setModel(QStringLiteral("mock-model"));
        connect(&summary, &Summarizer::changed, &summary, [&summary] { if (summary.busy()) summary.cancel(); });
        summary.summarizeRemaining();
        QVERIFY(!summary.busy());
        QTest::qWait(100);
        QCOMPARE(provider.requestCount, 0);
        QVERIFY(summary.summary().isEmpty());

        Atomicizer atomic(&app, &summary);
        connect(&atomic, &Atomicizer::changed, &atomic, [&atomic] { if (atomic.busy()) atomic.cancel(); });
        atomic.propose(app.cards().first().toMap().value(QStringLiteral("id")).toString());
        QVERIFY(!atomic.busy());
        QTest::qWait(100);
        QCOMPARE(provider.requestCount, 0);
        QVERIFY(!atomic.hasProposal());
        QCOMPARE(app.cards().size(), 2);
    }
    void atomicizeKeepNeverChangesTheCollection() {
        AppController app(m_temporary.filePath(QStringLiteral("atomic-keep"))); QVERIFY(seed(app));
        const QString id = app.cards().first().toMap().value(QStringLiteral("id")).toString();
        MockChat provider; QVERIFY(provider.listen(QHostAddress::LocalHost, 0));
        provider.body = atomicResponse(QStringLiteral("keep"));
        Summarizer configuration(&app); configuration.setEndpoint(provider.endpoint()); configuration.setModel(QStringLiteral("mock-model"));
        Atomicizer atomic(&app, &configuration); atomic.propose(id);
        QVERIFY(QTest::qWaitFor([&atomic] { return !atomic.busy(); }, 5000));
        QVERIFY2(atomic.error().isEmpty(), qPrintable(atomic.error()));
        QCOMPARE(atomic.decision(), QStringLiteral("keep"));
        QVERIFY(!atomic.hasProposal()); QVERIFY(atomic.proposals().isEmpty());
        QCOMPARE(app.cards().size(), 2);
        QVERIFY(!atomic.apply({}));
        QVERIFY(provider.requestHeaders.contains("Bearer mock-provider-key"));
        QVERIFY(provider.requestBody.contains("Private solution"));
        QVERIFY(!provider.requestBody.contains("mock-provider-key"));
    }
    void atomicizePreviewIsEditableAndOnlyCommitsOnApply() {
        AppController app(m_temporary.filePath(QStringLiteral("atomic-apply"))); QVERIFY(seed(app));
        const QVariantMap source = app.cards().first().toMap();
        const QString id = source.value(QStringLiteral("id")).toString();
        MockChat provider; QVERIFY(provider.listen(QHostAddress::LocalHost, 0));
        provider.body = atomicResponse(QStringLiteral("split"), atomicCards());
        Summarizer configuration(&app); configuration.setEndpoint(provider.endpoint()); configuration.setModel(QStringLiteral("mock-model"));
        Atomicizer atomic(&app, &configuration); QSignalSpy applied(&atomic, &Atomicizer::applied);
        atomic.propose(id);
        QVERIFY(QTest::qWaitFor([&atomic] { return !atomic.busy(); }, 5000));
        QVERIFY2(atomic.error().isEmpty(), qPrintable(atomic.error()));
        QVERIFY(atomic.hasProposal()); QCOMPARE(app.cards().size(), 2);
        QVariantList edited = atomic.proposals();
        QVariantMap first = edited.first().toMap(); first[QStringLiteral("front")] = QStringLiteral("Edited targeted question");
        edited[0] = first;
        QVERIFY(atomic.apply(edited));
        QVERIFY(QTest::qWaitFor([&atomic] { return !atomic.busy(); }, 5000));
        QCOMPARE(applied.size(), 1); QVERIFY2(atomic.error().isEmpty(), qPrintable(atomic.error()));
        QVERIFY(settle(app)); QCOMPARE(app.cards().size(), 3);
        bool editedSaved = false;
        for (const QVariant &entry : app.cards()) {
            const QVariantMap note = entry.toMap();
            QVERIFY(note.value(QStringLiteral("id")).toString() != id);
            if (note.value(QStringLiteral("front")).toString() == first.value(QStringLiteral("front")).toString()) editedSaved = true;
        }
        QVERIFY(editedSaved);
    }
    void atomicizeRejectsUnboundedOrInvalidProposals_data() {
        QTest::addColumn<QString>("decision");
        QTest::addColumn<QJsonArray>("cards");
        QTest::newRow("one-card") << QStringLiteral("split") << atomicCards(1);
        QTest::newRow("too-many") << QStringLiteral("split") << atomicCards(6);
        QTest::newRow("keep-with-cards") << QStringLiteral("keep") << atomicCards();
        QJsonArray duplicate = atomicCards(); duplicate[1] = duplicate[0];
        QTest::newRow("duplicates") << QStringLiteral("split") << duplicate;
        QJsonArray image = atomicCards();
        QJsonObject injected = image.first().toObject();
        injected[QStringLiteral("back")] = QStringLiteral("![invented](media:") + QStringLiteral("a").repeated(64) + QStringLiteral(".png)");
        image[0] = injected;
        QTest::newRow("invented-image") << QStringLiteral("split") << image;
    }
    void atomicizeRejectsUnboundedOrInvalidProposals() {
        QFETCH(QString, decision); QFETCH(QJsonArray, cards);
        QTemporaryDir directory; QVERIFY(directory.isValid());
        AppController app(directory.path()); QVERIFY(seed(app));
        MockChat provider; QVERIFY(provider.listen(QHostAddress::LocalHost, 0)); provider.body = atomicResponse(decision, cards);
        Summarizer configuration(&app); configuration.setEndpoint(provider.endpoint()); configuration.setModel(QStringLiteral("mock-model"));
        Atomicizer atomic(&app, &configuration); atomic.propose(app.cards().first().toMap().value(QStringLiteral("id")).toString());
        QVERIFY(QTest::qWaitFor([&atomic] { return !atomic.busy(); }, 5000));
        QVERIFY(atomic.error().startsWith(QStringLiteral("ATOMICIZE_")));
        QVERIFY(!atomic.hasProposal()); QCOMPARE(app.cards().size(), 2);
    }
    void atomicizeStaleSourceCannotOverwriteAnEdit() {
        AppController app(m_temporary.filePath(QStringLiteral("atomic-stale"))); QVERIFY(seed(app));
        const QVariantMap source = app.cards().first().toMap();
        const QString id = source.value(QStringLiteral("id")).toString();
        MockChat provider; QVERIFY(provider.listen(QHostAddress::LocalHost, 0)); provider.body = atomicResponse(QStringLiteral("split"), atomicCards());
        Summarizer configuration(&app); configuration.setEndpoint(provider.endpoint()); configuration.setModel(QStringLiteral("mock-model"));
        Atomicizer atomic(&app, &configuration); atomic.propose(id);
        QVERIFY(QTest::qWaitFor([&atomic] { return !atomic.busy(); }, 5000));
        QVERIFY(atomic.hasProposal());
        QVERIFY(app.saveCard(id, source.value(QStringLiteral("deckId")).toString(), source.value(QStringLiteral("kind")).toString(),
            QStringLiteral("A later edit"), source.value(QStringLiteral("back")).toString(), source.value(QStringLiteral("tags")).toString(), 1));
        QVERIFY(settle(app));
        QSignalSpy applied(&atomic, &Atomicizer::applied);
        QVERIFY(atomic.apply(atomic.proposals()));
        QVERIFY(QTest::qWaitFor([&atomic] { return !atomic.busy(); }, 5000));
        QVERIFY(atomic.error().startsWith(QStringLiteral("ATOMICIZE_SAVE")));
        QCOMPARE(applied.size(), 0); QCOMPARE(app.cards().size(), 2);
        bool kept = false;
        for (const QVariant &entry : app.cards())
            if (entry.toMap().value(QStringLiteral("id")).toString() == id) {
                QCOMPARE(entry.toMap().value(QStringLiteral("front")).toString(), QStringLiteral("A later edit")); kept = true;
            }
        QVERIFY(kept);
    }
    void atomicizeCancellationIgnoresTheLateResult() {
        AppController app(m_temporary.filePath(QStringLiteral("atomic-cancel"))); QVERIFY(seed(app));
        MockChat provider; QVERIFY(provider.listen(QHostAddress::LocalHost, 0));
        provider.body = atomicResponse(QStringLiteral("split"), atomicCards()); provider.delayMs = 300;
        Summarizer configuration(&app); configuration.setEndpoint(provider.endpoint()); configuration.setModel(QStringLiteral("mock-model"));
        Atomicizer atomic(&app, &configuration); atomic.propose(app.cards().first().toMap().value(QStringLiteral("id")).toString());
        QVERIFY(QTest::qWaitFor([&provider] { return provider.requestCount == 1; }, 5000));
        atomic.clear(); QTest::qWait(400);
        QVERIFY(!atomic.busy()); QVERIFY(!atomic.hasProposal()); QVERIFY(atomic.sourceCard().isEmpty());
        QCOMPARE(app.cards().size(), 2);
    }
    void keyringChangesPersistInOrder() {
        const QString bin = m_temporary.filePath(QStringLiteral("bin"));
        QVERIFY(QDir().mkpath(bin));
        const QString script = QDir(bin).filePath(QStringLiteral("secret-tool"));
        QFile output(script); QVERIFY(output.open(QIODevice::WriteOnly));
        const QByteArray program = R"(#!/usr/bin/python3
import os, pathlib, sys, time
target = pathlib.Path(os.environ['BETTERFLASH_FAKE_SECRET_FILE'])
operation = sys.argv[1]
if operation == 'lookup':
    time.sleep(0.1)
    if target.exists(): print(target.read_text(), end='')
elif operation == 'store':
    value = sys.stdin.read()
    if value.strip() == 'first': time.sleep(0.2)
    target.write_text(value)
elif operation == 'clear':
    target.unlink(missing_ok=True)
)";
        QCOMPARE(output.write(program), program.size()); output.close();
        QVERIFY(QFile::setPermissions(script, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner));
        const QString secretPath = m_temporary.filePath(QStringLiteral("fake-secret"));
        qputenv("BETTERFLASH_FAKE_SECRET_FILE", secretPath.toUtf8());
        qputenv("PATH", bin.toUtf8());
        SecretStore store(QStringLiteral("test-only"));
        store.setKey(QStringLiteral("first"));
        store.setKey(QStringLiteral("second"));
        QVERIFY(QTest::qWaitFor([&] {
            QFile saved(secretPath);
            return saved.open(QIODevice::ReadOnly) && saved.readAll().trimmed() == "second";
        }, 5000));
        QTest::qWait(150);
        QCOMPARE(store.key(), QStringLiteral("second"));
        store.setKey(QStringLiteral("third"));
        store.clear();
        QVERIFY(QTest::qWaitFor([&store] { return store.status().startsWith(QStringLiteral("Key removed")); }, 5000));
        QVERIFY(!QFile::exists(secretPath));
        QVERIFY(!store.hasKey());
        qputenv("PATH", m_originalPath);
        qunsetenv("BETTERFLASH_FAKE_SECRET_FILE");
    }
    void cleanupTestCase() {
        qputenv("PATH", m_originalPath);
        if (m_originalLlmKey.isNull()) qunsetenv("BETTERFLASH_LLM_API_KEY");
        else qputenv("BETTERFLASH_LLM_API_KEY", m_originalLlmKey);
        qunsetenv("BETTERFLASH_FAKE_SECRET_FILE");
    }
};

QTEST_MAIN(ProviderTest)
#include "provider_test.moc"
