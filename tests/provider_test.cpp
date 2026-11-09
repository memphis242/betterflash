#include "core/appcontroller.h"
#include "integrations/Atomicizer.h"
#include "integrations/CardGenerator.h"
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
#include <algorithm>

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
    bool configureRemote(Summarizer &summary, const QString &endpoint) {
        summary.setProvider(QStringLiteral("remote"));
        summary.setEndpoint(endpoint);
        summary.setModel(QStringLiteral("mock-model"));
        QObject *const keyBinding = summary.apiKey();
        if (!QTest::qWaitFor([keyBinding] { return !keyBinding->property("ownKeyId").toString().isEmpty(); }, 5000)) return false;
        summary.setApiKey(QStringLiteral("mock-provider-key"));
        return summary.hasApiKey();
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
    QByteArray generationResponse(const QString &front, const QString &back, int pointCount = 1) {
        const QJsonObject proposal{{QStringLiteral("decision"), QStringLiteral("generate")},
            {QStringLiteral("front"), front}, {QStringLiteral("back"), back},
            {QStringLiteral("pointCount"), pointCount}};
        const QJsonObject message{{QStringLiteral("content"),
            QString::fromUtf8(QJsonDocument(proposal).toJson(QJsonDocument::Compact))}};
        const QJsonArray choices{QJsonObject{{QStringLiteral("message"), message}}};
        return QJsonDocument(QJsonObject{{QStringLiteral("choices"), choices}}).toJson(QJsonDocument::Compact);
    }
    QByteArray generationRefusal(const QString &reason) {
        const QJsonObject refusal{{QStringLiteral("decision"), QStringLiteral("refuse")},
            {QStringLiteral("reason"), reason}};
        const QJsonObject message{{QStringLiteral("content"),
            QString::fromUtf8(QJsonDocument(refusal).toJson(QJsonDocument::Compact))}};
        const QJsonArray choices{QJsonObject{{QStringLiteral("message"), message}}};
        return QJsonDocument(QJsonObject{{QStringLiteral("choices"), choices}}).toJson(QJsonDocument::Compact);
    }
    QString addClozeSource(AppController &app) {
        if (!settle(app)) return {};
        app.createDeck(QStringLiteral("Cloze generation fixture"));
        if (!settle(app) || app.decks().isEmpty()) return {};
        const QString deck = app.decks().first().toMap().value(QStringLiteral("id")).toString();
        const QString front = QStringLiteral("[cloze1:Mercury] is closest to the Sun; [cloze2:Venus] follows it.");
        if (!app.saveCard({}, deck, QStringLiteral("cloze"), front, {}, {}, 1) || !settle(app)) return {};
        return app.cards().first().toMap().value(QStringLiteral("id")).toString();
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
        const QString providerBin = m_temporary.filePath(QStringLiteral("provider-bin"));
        QVERIFY(QDir().mkpath(providerBin));
        const QString secretTool = QDir(providerBin).filePath(QStringLiteral("secret-tool"));
        QFile secretToolFile(secretTool); QVERIFY(secretToolFile.open(QIODevice::WriteOnly));
        const QByteArray secretToolStub = "#!/bin/sh\nexit 1\n";
        QCOMPARE(secretToolFile.write(secretToolStub), secretToolStub.size()); secretToolFile.close();
        QVERIFY(QFile::setPermissions(secretTool, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner));
        m_originalPath = qgetenv("PATH");
        qputenv("PATH", providerBin.toUtf8() + ':' + m_originalPath);
        QCoreApplication::setOrganizationName(QStringLiteral("BetterFlashTest"));
        QCoreApplication::setApplicationName(QStringLiteral("Provider"));
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_temporary.filePath(QStringLiteral("settings")));
        m_originalLlmKey = qgetenv("BETTERFLASH_LLM_API_KEY");
        qputenv("BETTERFLASH_LLM_API_KEY", "mock-provider-key");
    }
    void summaryIncludesOnlyTheRemainingQuestions() {
        AppController app(m_temporary.filePath(QStringLiteral("summary")));
        QVERIFY(seed(app, 3));
        app.revealAnswer(); QVERIFY(settle(app)); app.grade(3); QVERIFY(settle(app));
        QCOMPARE(app.pendingCards().size(), 2);
        MockChat provider; QVERIFY(provider.listen(QHostAddress::LocalHost, 0));
        Summarizer summary(&app); QVERIFY(configureRemote(summary, provider.endpoint()));
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
    void similarCardClozeDraftPreservesNumberedGroupsAndUserInstructions() {
        QTemporaryDir directory; QVERIFY(directory.isValid());
        AppController app(directory.path());
        const QString sourceId = addClozeSource(app); QVERIFY(!sourceId.isEmpty());
        QCOMPARE(app.cards().size(), 1);
        MockChat provider; QVERIFY(provider.listen(QHostAddress::LocalHost, 0));
        provider.body = generationResponse(QStringLiteral("On a clear night, [cloze1:Mercury] appears near the Sun, while [cloze2:Venus] is farther away."), {}, 1);
        Summarizer configuration(&app); QVERIFY(configureRemote(configuration, provider.endpoint()));
        CardGenerator generator(&app, &configuration);
        QSignalSpy generated(&generator, &CardGenerator::generated);
        generator.generate(sourceId, QStringLiteral("Use an astronomy observation a student could make."));
        QVERIFY(QTest::qWaitFor([&generator] { return !generator.busy(); }, 5000));
        QVERIFY2(generator.error().isEmpty(), qPrintable(generator.error()));
        QCOMPARE(generated.size(), 1);
        const QVariantMap draft = generator.draft();
        QCOMPARE(draft.value(QStringLiteral("kind")).toString(), QStringLiteral("cloze"));
        QCOMPARE(draft.value(QStringLiteral("back")).toString(), QString{});
        QCOMPARE(app.cards().size(), 1);
        const QJsonObject request = QJsonDocument::fromJson(provider.requestBody).object();
        const QJsonArray messages = request.value(QStringLiteral("messages")).toArray();
        QVERIFY(messages.size() >= 2);
        const QString user = messages.last().toObject().value(QStringLiteral("content")).toString();
        const qsizetype jsonStart = user.indexOf(u'\n');
        QVERIFY(jsonStart >= 0);
        const QJsonObject requestData = QJsonDocument::fromJson(user.sliced(jsonStart + 1).toUtf8()).object();
        QVERIFY(requestData.contains(QStringLiteral("sourceCard")));
        QVERIFY(requestData.contains(QStringLiteral("userInstructions")));
        QCOMPARE(requestData.value(QStringLiteral("userInstructions")).toString(),
                 QStringLiteral("Use an astronomy observation a student could make."));
        const QJsonObject sourceCard = requestData.value(QStringLiteral("sourceCard")).toObject();
        QCOMPARE(sourceCard.value(QStringLiteral("kind")).toString(), QStringLiteral("cloze"));
        QVERIFY(sourceCard.value(QStringLiteral("front")).toString().contains(QStringLiteral("Mercury")));
        QVERIFY(!provider.requestBody.contains("mock-provider-key"));

        QVERIFY(app.saveCard({}, draft.value(QStringLiteral("deckId")).toString(),
            draft.value(QStringLiteral("kind")).toString(), draft.value(QStringLiteral("front")).toString(),
            draft.value(QStringLiteral("back")).toString(), draft.value(QStringLiteral("tags")).toString(),
            draft.value(QStringLiteral("pointCount")).toInt()));
        QVERIFY(settle(app));
        QCOMPARE(app.cards().size(), 2);
        QVariantMap saved;
        for (const QVariant &entry : app.cards()) {
            const QVariantMap card = entry.toMap();
            if (card.value(QStringLiteral("front")).toString() == draft.value(QStringLiteral("front")).toString())
                saved = card;
        }
        QVERIFY(!saved.isEmpty());
        const QVariantList variants = saved.value(QStringLiteral("variants")).toList();
        QCOMPARE(variants.size(), 2);
        QStringList keys;
        for (const QVariant &entry : variants) {
            const QVariantMap variant = entry.toMap();
            keys.append(variant.value(QStringLiteral("key")).toString());
            QCOMPARE(variant.value(QStringLiteral("reviewCount")).toInt(), 0);
            QVERIFY(!variant.value(QStringLiteral("graduated")).toBool());
        }
        std::sort(keys.begin(), keys.end());
        QCOMPARE(keys, QStringList({QStringLiteral("c1"), QStringLiteral("c2")}));
        bool sourceUnchanged = false;
        for (const QVariant &entry : app.cards()) {
            const QVariantMap card = entry.toMap();
            if (card.value(QStringLiteral("id")).toString() == sourceId)
                sourceUnchanged = card.value(QStringLiteral("front")).toString()
                    == QStringLiteral("[cloze1:Mercury] is closest to the Sun; [cloze2:Venus] follows it.");
        }
        QVERIFY(sourceUnchanged);
    }
    void similarCardClozeDraftRejectsMalformedNumberedGroups() {
        QTemporaryDir directory; QVERIFY(directory.isValid());
        AppController app(directory.path());
        const QString sourceId = addClozeSource(app); QVERIFY(!sourceId.isEmpty());
        MockChat provider; QVERIFY(provider.listen(QHostAddress::LocalHost, 0));
        provider.body = generationResponse(QStringLiteral("[cloze0:Invalid] is not a valid cloze group."), {}, 1);
        Summarizer configuration(&app); QVERIFY(configureRemote(configuration, provider.endpoint()));
        CardGenerator generator(&app, &configuration);
        generator.generate(sourceId);
        QVERIFY(QTest::qWaitFor([&generator] { return !generator.busy(); }, 5000));
        QVERIFY(!generator.error().isEmpty());
        QVERIFY(generator.draft().isEmpty());
        QCOMPARE(app.cards().size(), 1);
    }
    void similarCardRejectsOversizedUserInstructionsBeforeProviderCall() {
        QTemporaryDir directory; QVERIFY(directory.isValid());
        AppController app(directory.path());
        const QString sourceId = addClozeSource(app); QVERIFY(!sourceId.isEmpty());
        MockChat provider; QVERIFY(provider.listen(QHostAddress::LocalHost, 0));
        Summarizer configuration(&app); QVERIFY(configureRemote(configuration, provider.endpoint()));
        CardGenerator generator(&app, &configuration);
        generator.generate(sourceId, QString(2001, QLatin1Char('x')));
        QVERIFY(!generator.busy());
        QVERIFY(!generator.error().isEmpty());
        QCOMPARE(provider.requestCount, 0);
        QCOMPARE(app.cards().size(), 1);
    }
    void similarCardBasicDraftCanBeSavedWithoutChangingItsSource() {
        QTemporaryDir directory; QVERIFY(directory.isValid());
        AppController app(directory.path()); QVERIFY(seed(app, 1));
        const QVariantMap source = app.cards().first().toMap();
        const QString sourceId = source.value(QStringLiteral("id")).toString();
        MockChat provider; QVERIFY(provider.listen(QHostAddress::LocalHost, 0));
        provider.body = generationResponse(QStringLiteral("A different example of the same concept?"),
                                           QStringLiteral("The independently solved answer."));
        Summarizer configuration(&app); QVERIFY(configureRemote(configuration, provider.endpoint()));
        CardGenerator generator(&app, &configuration);
        generator.generate(sourceId);
        QVERIFY(QTest::qWaitFor([&generator] { return !generator.busy(); }, 5000));
        QVERIFY2(generator.error().isEmpty(), qPrintable(generator.error()));
        QCOMPARE(generator.draft().value(QStringLiteral("kind")).toString(), QStringLiteral("basic"));
        QCOMPARE(app.cards().size(), 1);
        const QVariantMap draft = generator.draft();
        QVERIFY(app.saveCard({}, draft.value(QStringLiteral("deckId")).toString(),
            draft.value(QStringLiteral("kind")).toString(), draft.value(QStringLiteral("front")).toString(),
            draft.value(QStringLiteral("back")).toString(), draft.value(QStringLiteral("tags")).toString(),
            draft.value(QStringLiteral("pointCount")).toInt()));
        QVERIFY(settle(app));
        QCOMPARE(app.cards().size(), 2);
        bool originalPreserved = false;
        for (const QVariant &entry : app.cards()) {
            const QVariantMap card = entry.toMap();
            if (card.value(QStringLiteral("id")).toString() == sourceId)
                originalPreserved = card.value(QStringLiteral("front")).toString()
                    == source.value(QStringLiteral("front")).toString();
        }
        QVERIFY(originalPreserved);
    }
    void similarCardRefinementUsesEditedDraftAndKeepsLastSuccessfulResult() {
        QTemporaryDir directory; QVERIFY(directory.isValid());
        AppController app(directory.path()); QVERIFY(seed(app, 1));
        const QVariantMap source = app.cards().first().toMap();
        const QString sourceId = source.value(QStringLiteral("id")).toString();
        MockChat provider; QVERIFY(provider.listen(QHostAddress::LocalHost, 0));
        provider.body = generationResponse(QStringLiteral("Initial generated question?"),
                                           QStringLiteral("Initial generated answer."));
        Summarizer configuration(&app); QVERIFY(configureRemote(configuration, provider.endpoint()));
        CardGenerator generator(&app, &configuration);
        generator.generate(sourceId);
        QVERIFY(QTest::qWaitFor([&generator] { return !generator.busy(); }, 5000));
        QVERIFY2(generator.error().isEmpty(), qPrintable(generator.error()));
        QVariantMap currentDraft = generator.draft();
        QVERIFY(!currentDraft.isEmpty());
        currentDraft.insert(QStringLiteral("front"), QStringLiteral("Edited before the first refinement?"));
        currentDraft.insert(QStringLiteral("back"), QStringLiteral("User-edited answer before round one."));
        currentDraft.insert(QStringLiteral("tags"), QStringLiteral("manual,round-one"));
        currentDraft.insert(QStringLiteral("pointCount"), 2);

        provider.body = generationResponse(QStringLiteral("First refined question?"),
                                           QStringLiteral("First refined answer."), 3);
        generator.refine(currentDraft, QStringLiteral("Use one concrete example."));
        QVERIFY(QTest::qWaitFor([&generator] { return !generator.busy(); }, 5000));
        QVERIFY2(generator.error().isEmpty(), qPrintable(generator.error()));
        QVariantMap firstRefinement = generator.draft();
        QCOMPARE(firstRefinement.value(QStringLiteral("front")).toString(), QStringLiteral("First refined question?"));
        QCOMPARE(firstRefinement.value(QStringLiteral("tags")).toString(), QStringLiteral("manual,round-one"));
        QCOMPARE(firstRefinement.value(QStringLiteral("pointCount")).toInt(), 3);

        const QJsonObject firstRequest = QJsonDocument::fromJson(provider.requestBody).object();
        const QString firstUser = firstRequest.value(QStringLiteral("messages")).toArray().last()
                                      .toObject().value(QStringLiteral("content")).toString();
        const qsizetype firstJsonStart = firstUser.indexOf(u'\n');
        QVERIFY(firstJsonStart >= 0);
        const QJsonObject firstData = QJsonDocument::fromJson(firstUser.sliced(firstJsonStart + 1).toUtf8()).object();
        QCOMPARE(firstData.value(QStringLiteral("userInstructions")).toString(), QStringLiteral("Use one concrete example."));
        const QJsonObject firstRequestDraft = firstData.value(QStringLiteral("currentDraft")).toObject();
        QCOMPARE(firstRequestDraft.value(QStringLiteral("front")).toString(), QStringLiteral("Edited before the first refinement?"));
        QCOMPARE(firstRequestDraft.value(QStringLiteral("back")).toString(), QStringLiteral("User-edited answer before round one."));
        QCOMPARE(firstRequestDraft.value(QStringLiteral("tags")).toString(), QStringLiteral("manual,round-one"));
        QVERIFY(firstData.value(QStringLiteral("sourceCard")).toObject().value(QStringLiteral("front")).toString()
                    .contains(source.value(QStringLiteral("front")).toString()));
        QVERIFY(!provider.requestBody.contains("mock-provider-key"));

        firstRefinement.insert(QStringLiteral("front"), source.value(QStringLiteral("front")).toString());
        firstRefinement.insert(QStringLiteral("back"), QStringLiteral("Manual detail to retain for round two."));
        firstRefinement.insert(QStringLiteral("tags"), QStringLiteral("manual,round-two"));
        provider.body = generationResponse(source.value(QStringLiteral("front")).toString(),
                                           QStringLiteral("Second refined answer."), 4);
        generator.refine(firstRefinement, QStringLiteral("Keep the user-supplied terminology."));
        QVERIFY(QTest::qWaitFor([&generator] { return !generator.busy(); }, 5000));
        QVERIFY2(generator.error().isEmpty(), qPrintable(generator.error()));
        QVariantMap lastSuccessfulDraft = generator.draft();
        QCOMPARE(lastSuccessfulDraft.value(QStringLiteral("front")).toString(), source.value(QStringLiteral("front")).toString());
        QCOMPARE(lastSuccessfulDraft.value(QStringLiteral("tags")).toString(), QStringLiteral("manual,round-two"));
        QCOMPARE(lastSuccessfulDraft.value(QStringLiteral("pointCount")).toInt(), 4);
        const QJsonObject secondRequest = QJsonDocument::fromJson(provider.requestBody).object();
        QCOMPARE(secondRequest.value(QStringLiteral("messages")).toArray().size(), 2);
        const QString secondUser = secondRequest.value(QStringLiteral("messages")).toArray().last()
                                       .toObject().value(QStringLiteral("content")).toString();
        const qsizetype secondJsonStart = secondUser.indexOf(u'\n');
        QVERIFY(secondJsonStart >= 0);
        const QJsonObject secondData = QJsonDocument::fromJson(secondUser.sliced(secondJsonStart + 1).toUtf8()).object();
        QCOMPARE(secondData.value(QStringLiteral("userInstructions")).toString(), QStringLiteral("Keep the user-supplied terminology."));
        const QJsonObject secondRequestDraft = secondData.value(QStringLiteral("currentDraft")).toObject();
        QCOMPARE(secondRequestDraft.value(QStringLiteral("front")).toString(), source.value(QStringLiteral("front")).toString());
        QCOMPARE(secondRequestDraft.value(QStringLiteral("back")).toString(), QStringLiteral("Manual detail to retain for round two."));
        QCOMPARE(secondRequestDraft.value(QStringLiteral("tags")).toString(), QStringLiteral("manual,round-two"));
        QVERIFY(secondData.value(QStringLiteral("sourceCard")).toObject().value(QStringLiteral("front")).toString()
                    .contains(source.value(QStringLiteral("front")).toString()));

        const int beforeFailedRefinement = provider.requestCount;
        provider.status = 503;
        provider.body = QByteArrayLiteral("{\"error\":{\"message\":\"temporary provider failure\"}}");
        generator.refine(lastSuccessfulDraft, QStringLiteral("Try a failed refinement."));
        QVERIFY(QTest::qWaitFor([&generator] { return !generator.busy(); }, 5000));
        QVERIFY(generator.error().contains(QStringLiteral("503")));
        QCOMPARE(provider.requestCount, beforeFailedRefinement + 1);
        QCOMPARE(generator.draft(), lastSuccessfulDraft);
        QCOMPARE(app.cards().size(), 1);

        provider.status = 200;
        provider.delayMs = 300;
        generator.refine(lastSuccessfulDraft, QStringLiteral("Cancel this refinement."));
        QVERIFY(QTest::qWaitFor([&provider, beforeFailedRefinement] {
            return provider.requestCount == beforeFailedRefinement + 2;
        }, 5000));
        QVERIFY(generator.busy());
        generator.cancel();
        QVERIFY(!generator.busy());
        QCOMPARE(generator.draft(), lastSuccessfulDraft);
        QTest::qWait(provider.delayMs + 50);
        QCOMPARE(generator.draft(), lastSuccessfulDraft);
        QCOMPARE(app.cards().size(), 1);

        QVariantMap accepted = generator.draft();
        accepted.insert(QStringLiteral("front"), QStringLiteral("Accepted after a final manual edit?"));
        accepted.insert(QStringLiteral("back"), QStringLiteral("Accepted answer with the user's correction."));
        QVERIFY(app.saveCard({}, accepted.value(QStringLiteral("deckId")).toString(),
            accepted.value(QStringLiteral("kind")).toString(), accepted.value(QStringLiteral("front")).toString(),
            accepted.value(QStringLiteral("back")).toString(), accepted.value(QStringLiteral("tags")).toString(),
            accepted.value(QStringLiteral("pointCount")).toInt()));
        QVERIFY(settle(app));
        QCOMPARE(app.cards().size(), 2);
        bool acceptedFieldsSaved = false;
        bool sourceUnchanged = false;
        for (const QVariant &entry : app.cards()) {
            const QVariantMap card = entry.toMap();
            if (card.value(QStringLiteral("id")).toString() == sourceId)
                sourceUnchanged = card.value(QStringLiteral("front")).toString()
                        == source.value(QStringLiteral("front")).toString()
                    && card.value(QStringLiteral("back")).toString() == source.value(QStringLiteral("back")).toString();
            if (card.value(QStringLiteral("front")).toString() == accepted.value(QStringLiteral("front")).toString())
                acceptedFieldsSaved = card.value(QStringLiteral("back")).toString()
                        == accepted.value(QStringLiteral("back")).toString()
                    && card.value(QStringLiteral("pointCount")).toInt() == accepted.value(QStringLiteral("pointCount")).toInt();
        }
        QVERIFY(acceptedFieldsSaved);
        QVERIFY(sourceUnchanged);
    }
    void similarCardRefinementValidatesAnEditedClozeKind() {
        QTemporaryDir directory; QVERIFY(directory.isValid());
        AppController app(directory.path()); QVERIFY(seed(app, 1));
        const QString sourceId = app.cards().first().toMap().value(QStringLiteral("id")).toString();
        MockChat provider; QVERIFY(provider.listen(QHostAddress::LocalHost, 0));
        provider.body = generationResponse(QStringLiteral("A generated basic question?"),
                                           QStringLiteral("A generated answer."));
        Summarizer configuration(&app); QVERIFY(configureRemote(configuration, provider.endpoint()));
        CardGenerator generator(&app, &configuration);
        generator.generate(sourceId);
        QVERIFY(QTest::qWaitFor([&generator] { return !generator.busy(); }, 5000));
        QVERIFY2(generator.error().isEmpty(), qPrintable(generator.error()));

        QVariantMap edited = generator.draft();
        edited.insert(QStringLiteral("kind"), QStringLiteral("cloze"));
        edited.insert(QStringLiteral("front"), QStringLiteral("The student crosses [cloze1:the Atlantic]."));
        edited.insert(QStringLiteral("back"), QString{});
        provider.body = generationResponse(QStringLiteral("A new cloze note with [cloze1:a numbered group] and [cloze2:another group]."), {}, 2);
        generator.refine(edited, QStringLiteral("Keep the card as cloze."));
        QVERIFY(QTest::qWaitFor([&generator] { return !generator.busy(); }, 5000));
        QVERIFY2(generator.error().isEmpty(), qPrintable(generator.error()));
        const QVariantMap accepted = generator.draft();
        QCOMPARE(accepted.value(QStringLiteral("kind")).toString(), QStringLiteral("cloze"));
        QCOMPARE(accepted.value(QStringLiteral("back")).toString(), QString{});
        QCOMPARE(app.cards().size(), 1);

        QVERIFY(app.saveCard({}, accepted.value(QStringLiteral("deckId")).toString(),
            accepted.value(QStringLiteral("kind")).toString(), accepted.value(QStringLiteral("front")).toString(),
            accepted.value(QStringLiteral("back")).toString(), accepted.value(QStringLiteral("tags")).toString(),
            accepted.value(QStringLiteral("pointCount")).toInt()));
        QVERIFY(settle(app));
        QCOMPARE(app.cards().size(), 2);
        QVariantMap saved;
        for (const QVariant &entry : app.cards()) {
            const QVariantMap card = entry.toMap();
            if (card.value(QStringLiteral("front")).toString() == accepted.value(QStringLiteral("front")).toString())
                saved = card;
        }
        QCOMPARE(saved.value(QStringLiteral("kind")).toString(), QStringLiteral("cloze"));
        const QVariantList variants = saved.value(QStringLiteral("variants")).toList();
        QCOMPARE(variants.size(), 2);
        QStringList keys;
        for (const QVariant &entry : variants) keys.append(entry.toMap().value(QStringLiteral("key")).toString());
        std::sort(keys.begin(), keys.end());
        QCOMPARE(keys, QStringList({QStringLiteral("c1"), QStringLiteral("c2")}));
    }
    void similarCardClozeRefinementPreservesClozeKindAndNumberedGroups() {
        QTemporaryDir directory; QVERIFY(directory.isValid());
        AppController app(directory.path());
        const QString sourceId = addClozeSource(app); QVERIFY(!sourceId.isEmpty());
        MockChat provider; QVERIFY(provider.listen(QHostAddress::LocalHost, 0));
        provider.body = generationResponse(QStringLiteral("[cloze1:Mercury] circles the Sun."), {}, 1);
        Summarizer configuration(&app); QVERIFY(configureRemote(configuration, provider.endpoint()));
        CardGenerator generator(&app, &configuration);
        generator.generate(sourceId);
        QVERIFY(QTest::qWaitFor([&generator] { return !generator.busy(); }, 5000));
        QVERIFY2(generator.error().isEmpty(), qPrintable(generator.error()));
        QVariantMap edited = generator.draft();
        edited.insert(QStringLiteral("front"), QStringLiteral("Student edit: [cloze1:Mercury] orbits."));
        provider.body = generationResponse(QStringLiteral("A year on [cloze1:Mercury] is shorter than one on [cloze2:Venus]."), {}, 2);
        generator.refine(edited, QStringLiteral("Keep this as a cloze card."));
        QVERIFY(QTest::qWaitFor([&generator] { return !generator.busy(); }, 5000));
        QVERIFY2(generator.error().isEmpty(), qPrintable(generator.error()));
        QCOMPARE(generator.draft().value(QStringLiteral("kind")).toString(), QStringLiteral("cloze"));
        QCOMPARE(generator.draft().value(QStringLiteral("back")).toString(), QString{});
        QCOMPARE(generator.draft().value(QStringLiteral("pointCount")).toInt(), 2);
        QVERIFY(generator.draft().value(QStringLiteral("front")).toString().contains(QStringLiteral("[cloze1:")));
        QVERIFY(generator.draft().value(QStringLiteral("front")).toString().contains(QStringLiteral("[cloze2:")));
        QCOMPARE(app.cards().size(), 1);
    }
    void similarCardRefusalReturnsActionableErrorAndNoDraft() {
        QTemporaryDir directory; QVERIFY(directory.isValid());
        AppController app(directory.path()); QVERIFY(seed(app, 1));
        const QString sourceId = app.cards().first().toMap().value(QStringLiteral("id")).toString();
        MockChat provider; QVERIFY(provider.listen(QHostAddress::LocalHost, 0));
        provider.body = generationRefusal(QStringLiteral("The source question is too ambiguous to vary responsibly."));
        Summarizer configuration(&app); QVERIFY(configureRemote(configuration, provider.endpoint()));
        CardGenerator generator(&app, &configuration);
        generator.generate(sourceId);
        QVERIFY(QTest::qWaitFor([&generator] { return !generator.busy(); }, 5000));
        QVERIFY(generator.error().startsWith(QStringLiteral("CARD_GENERATION_REFUSED:")));
        QVERIFY(generator.draft().isEmpty());
        QCOMPARE(app.cards().size(), 1);
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
        Summarizer summary(&app); QVERIFY(configureRemote(summary, provider.endpoint()));
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
        Summarizer summary(&app); QVERIFY(configureRemote(summary, provider.endpoint()));
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
        Summarizer summary(&app); summary.setProvider(QStringLiteral("remote"));
        summary.setEndpoint(QStringLiteral("http://example.invalid/chat/completions"));
        summary.summarizeRemaining();
        QVERIFY(QTest::qWaitFor([&summary] { return !summary.busy(); }, 5000));
        QVERIFY(summary.error().startsWith(QStringLiteral("LLM_ENDPOINT")));
    }
    void synchronousNotificationCancellationStopsTheRequest() {
        AppController app(m_temporary.filePath(QStringLiteral("notification-cancel"))); QVERIFY(seed(app));
        MockChat provider; QVERIFY(provider.listen(QHostAddress::LocalHost, 0));
        provider.delayMs = 300;
        provider.body = atomicResponse(QStringLiteral("split"), atomicCards());
        Summarizer summary(&app); QVERIFY(configureRemote(summary, provider.endpoint()));
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
        Summarizer configuration(&app); QVERIFY(configureRemote(configuration, provider.endpoint()));
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
        Summarizer configuration(&app); QVERIFY(configureRemote(configuration, provider.endpoint()));
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
        Summarizer configuration(&app); QVERIFY(configureRemote(configuration, provider.endpoint()));
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
        Summarizer configuration(&app); QVERIFY(configureRemote(configuration, provider.endpoint()));
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
        Summarizer configuration(&app); QVERIFY(configureRemote(configuration, provider.endpoint()));
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
    void environmentCredentialsAreExplicitlyReadOnly() {
        const QByteArray variable("BETTERFLASH_TEST_READ_ONLY_KEY");
        qputenv(variable.constData(), "environment-secret");
        SecretStore store(QStringLiteral("test-environment"), QString::fromLatin1(variable));
        QCOMPARE(store.storageState(), QStringLiteral("environment"));
        store.setKey(QStringLiteral("replacement"));
        QCOMPARE(store.key(), QStringLiteral("environment-secret"));
        QCOMPARE(store.storageState(), QStringLiteral("environment"));
        store.clear();
        QCOMPARE(store.key(), QStringLiteral("environment-secret"));
        QCOMPARE(store.storageState(), QStringLiteral("environment"));
        qunsetenv(variable.constData());
    }
    void keyringDestructionDrainsPendingRemoval() {
        const QString bin = m_temporary.filePath(QStringLiteral("shutdown-bin"));
        QVERIFY(QDir().mkpath(bin));
        const QString script = QDir(bin).filePath(QStringLiteral("secret-tool"));
        QFile output(script); QVERIFY(output.open(QIODevice::WriteOnly));
        const QByteArray program = R"(#!/usr/bin/python3
import os, pathlib, sys, time
target = pathlib.Path(os.environ['BETTERFLASH_FAKE_SECRET_FILE'])
if sys.argv[1] == 'store':
    time.sleep(0.1)
    target.write_text(sys.stdin.read())
elif sys.argv[1] == 'clear':
    time.sleep(0.1)
    target.unlink(missing_ok=True)
)";
        QCOMPARE(output.write(program), program.size()); output.close();
        QVERIFY(QFile::setPermissions(script, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner));
        const QString secretPath = m_temporary.filePath(QStringLiteral("shutdown-secret"));
        qputenv("BETTERFLASH_FAKE_SECRET_FILE", secretPath.toUtf8());
        qputenv("PATH", bin.toUtf8());
        QFile initial(secretPath); QVERIFY(initial.open(QIODevice::WriteOnly));
        QCOMPARE(initial.write("saved-secret"), qint64(12)); initial.close();
        {
            SecretStore store(QStringLiteral("test-shutdown"));
            store.setKey(QStringLiteral("replacement-secret"));
            store.clear();
        }
        QVERIFY(!QFile::exists(secretPath));
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
