#include "core/appcontroller.h"
#include "core/cardmodel.h"
#include "core/mediabackup.h"
#include "integrations/MediaStore.h"
#include "integrations/ShortcutManager.h"
#include "integrations/SyncController.h"
#include <QBuffer>
#include <QCryptographicHash>
#include <QDir>
#include <QImage>
#include <QJsonDocument>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QSettings>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTimer>
#include <QtTest>
#include <functional>

namespace {
struct HttpRequest {
    QByteArray method;
    QByteArray path;
    QByteArray body;
};
struct HttpResponse {
    QByteArray body;
    int status = 200;
    int delayMs = 0;
};
class LocalHttpFixture final : public QObject {
public:
    using Handler = std::function<struct HttpResponse(const struct HttpRequest &)>;
    explicit LocalHttpFixture(Handler handler) : m_handler(std::move(handler)) {
        connect(&m_server, &QTcpServer::newConnection, this, [this] {
            while (m_server.hasPendingConnections()) {
                QTcpSocket *const socket = m_server.nextPendingConnection();
                connect(socket, &QTcpSocket::readyRead, this, [this, socket] {
                    QByteArray &buffer = m_buffers[socket];
                    buffer += socket->readAll();
                    const qsizetype headerEnd = buffer.indexOf("\r\n\r\n");
                    if (headerEnd < 0) return;
                    const QList<QByteArray> headers = buffer.left(headerEnd).split('\n');
                    qint64 length = 0;
                    for (const QByteArray &header : headers) {
                        if (header.toLower().startsWith("content-length:")) length = header.mid(15).trimmed().toLongLong();
                    }
                    if (length < 0 || length > 4 * 1024 * 1024) { socket->disconnectFromHost(); return; }
                    if (buffer.size() - headerEnd - 4 < length) return;
                    const QList<QByteArray> line = headers.first().trimmed().split(' ');
                    if (line.size() < 2) { socket->disconnectFromHost(); return; }
                    const struct HttpRequest request{line[0], line[1], buffer.mid(headerEnd + 4, length)};
                    m_buffers.remove(socket);
                    socket->disconnect(this);
                    connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
                    requests.append(request);
                    const struct HttpResponse response = m_handler(request);
                    const auto send = [socket, response] {
                        socket->write("HTTP/1.1 " + QByteArray::number(response.status) + " Fixture\r\nContent-Length: "
                                      + QByteArray::number(response.body.size()) + "\r\nConnection: close\r\n\r\n" + response.body);
                        socket->disconnectFromHost();
                    };
                    if (response.delayMs > 0) QTimer::singleShot(response.delayMs, socket, send);
                    else send();
                });
                connect(socket, &QTcpSocket::disconnected, this, [this, socket] { m_buffers.remove(socket); socket->deleteLater(); });
            }
        });
    }
    bool listen() { return m_server.listen(QHostAddress::LocalHost, 0); }
    QString endpoint() const { return QStringLiteral("http://127.0.0.1:%1/v1/sync").arg(m_server.serverPort()); }
    QList<struct HttpRequest> requests;
private:
    QTcpServer m_server;
    const Handler m_handler;
    QHash<QTcpSocket *, QByteArray> m_buffers;
};
QByteArray json(const QVariantMap &value) { return QJsonDocument::fromVariant(value).toJson(QJsonDocument::Compact); }
QVariantMap emptyResponse(const struct HttpRequest &request) {
    const QVariantMap batch = QJsonDocument::fromJson(request.body).toVariant().toMap();
    return {{QStringLiteral("acceptedIds"), QVariantList{}}, {QStringLiteral("events"), QVariantList{}},
            {QStringLiteral("cursor"), batch.value(QStringLiteral("cursor"))}, {QStringLiteral("hasMore"), false}};
}
QVariantMap cardEvent(const QString &front) {
    const struct betterflash::model::Card card{betterflash::model::uuid(), betterflash::model::uuid(),
        QStringLiteral("basic"), front, QStringLiteral("An answer"), QStringLiteral(""), 1};
    const struct betterflash::model::Variant variant{betterflash::model::variantId(card.id, QStringLiteral("forward")),
        card.id, QStringLiteral("forward"), betterflash::model::nowUtc(), 0, 1.0, 0.5};
    return {{QStringLiteral("id"), betterflash::model::uuid()}, {QStringLiteral("deviceId"), betterflash::model::uuid()},
        {QStringLiteral("type"), QStringLiteral("card.upsert")}, {QStringLiteral("createdAt"), betterflash::model::nowUtc()},
        {QStringLiteral("payload"), QVariantMap{{QStringLiteral("card"), betterflash::model::toMap(card)},
            {QStringLiteral("variants"), QVariantList{betterflash::model::toMap(variant)}}}}};
}
QVariantMap responseWithEvent(const QVariantMap &event) {
    return {{QStringLiteral("acceptedIds"), QVariantList{}},
        {QStringLiteral("events"), QVariantList{QVariantMap{{QStringLiteral("seq"), 1}, {QStringLiteral("event"), event}}}},
        {QStringLiteral("cursor"), 1}, {QStringLiteral("hasMore"), false}};
}
QString imageName(const QByteArray &bytes) {
    return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex()) + QStringLiteral(".png");
}
QByteArray smallImage() {
    QImage image(16, 16, QImage::Format_RGB32);
    image.fill(Qt::green);
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    return bytes;
}
}

class IntegrationTest final : public QObject {
    Q_OBJECT
private:
    QTemporaryDir m_temporary;
    QProcess m_server;
    QString m_endpoint;
    bool waitIdle(AppController &app) {
        const bool settled = QTest::qWaitFor([&app] { return !app.busy(); }, 10000);
        if (!app.lastError().isEmpty()) qWarning().noquote() << QJsonDocument::fromVariant(app.lastError()).toJson(QJsonDocument::Compact);
        return settled && app.lastError().isEmpty();
    }
    bool synchronize(SyncController &sync) {
        sync.synchronize();
        return QTest::qWaitFor([&sync] { return !sync.busy(); }, 30000) && sync.error().isEmpty();
    }
private slots:
    void initTestCase() {
        QVERIFY(m_temporary.isValid());
        QCoreApplication::setOrganizationName(QStringLiteral("BetterFlashTest"));
        QCoreApplication::setApplicationName(QStringLiteral("Integrations"));
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_temporary.filePath(QStringLiteral("settings")));
        const QByteArray token("local-integration-test-token-0123456789");
        qputenv("BETTERFLASH_SYNC_TOKEN", token);
        QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
        environment.insert(QStringLiteral("BETTERFLASH_SYNC_TOKEN"), QString::fromLatin1(token));
        environment.remove(QStringLiteral("GROQ_API_KEY"));
        environment.remove(QStringLiteral("BETTERFLASH_LLM_API_KEY"));
        m_server.setProcessEnvironment(environment);
        m_server.start(QStringLiteral("python3"), {QStringLiteral(BETTERFLASH_SOURCE_DIR "/server/main.py"),
            QStringLiteral("--port"), QStringLiteral("0"), QStringLiteral("--database"), m_temporary.filePath(QStringLiteral("server.sqlite")),
            QStringLiteral("--media"), m_temporary.filePath(QStringLiteral("server-media"))});
        QVERIFY(m_server.waitForStarted(5000));
        QVERIFY(m_server.waitForReadyRead(5000));
        const QString message = QString::fromUtf8(m_server.readAllStandardOutput());
        const auto match = QRegularExpression(QStringLiteral("http://127\\.0\\.0\\.1:[0-9]+")).match(message);
        QVERIFY(match.hasMatch());
        m_endpoint = match.captured() + QStringLiteral("/v1/sync");
    }
    void deviceSyncTransfersNotesImagesAndReviews() {
        AppController first(m_temporary.filePath(QStringLiteral("first")));
        AppController second(m_temporary.filePath(QStringLiteral("second")));
        QVERIFY(waitIdle(first)); QVERIFY(waitIdle(second));
        MediaStore firstMedia(m_temporary.filePath(QStringLiteral("first")));
        QImage image(40, 20, QImage::Format_RGB32); image.fill(Qt::green);
        const QString input = m_temporary.filePath(QStringLiteral("input.png"));
        QVERIFY(image.save(input));
        QSignalSpy imported(&firstMedia, &MediaStore::imageImported);
        firstMedia.importImage(QUrl::fromLocalFile(input), QStringLiteral("diagram"));
        QVERIFY(imported.wait(5000));
        QVERIFY(firstMedia.error().isEmpty());
        const QString imageMarkdown = imported.first().first().toString();
        first.createDeck(QStringLiteral("Two-device collection"));
        QVERIFY(waitIdle(first));
        const QString deckId = first.decks().first().toMap().value(QStringLiteral("id")).toString();
        QVERIFY(first.saveCard({}, deckId, QStringLiteral("reverse"), imageMarkdown, QStringLiteral("A diagram"), {}, 2));
        QVERIFY(waitIdle(first));
        SyncController firstSync(&first, firstMedia.rootPath());
        SyncController secondSync(&second, second.mediaPath());
        firstSync.setEndpoint(m_endpoint); secondSync.setEndpoint(m_endpoint);
        QVERIFY2(synchronize(firstSync), qPrintable(firstSync.error()));
        QVERIFY2(synchronize(secondSync), qPrintable(secondSync.error()));
        QCOMPARE(second.cards().size(), 1);
        QCOMPARE(second.decks().size(), 1);
        QCOMPARE(QDir(second.mediaPath()).entryList(QDir::Files).size(), 1);
        const QString assetName = QDir(second.mediaPath()).entryList(QDir::Files).first();
        QFile damaged(QDir(second.mediaPath()).filePath(assetName));
        QVERIFY(damaged.open(QIODevice::WriteOnly | QIODevice::Truncate));
        QCOMPARE(damaged.write("damaged image"), qint64(13)); damaged.close();
        const QString originalSource = first.cards().first().toMap().value(QStringLiteral("id")).toString();
        QVERIFY(first.saveCard(originalSource, deckId, QStringLiteral("reverse"), imageMarkdown + QStringLiteral("\n\nUpdated prompt"),
            QStringLiteral("A diagram"), {}, 2));
        QVERIFY(waitIdle(first));
        QVERIFY2(synchronize(firstSync), qPrintable(firstSync.error()));
        QVERIFY2(synchronize(secondSync), qPrintable(secondSync.error()));
        QFile repaired(QDir(second.mediaPath()).filePath(assetName)); QVERIFY(repaired.open(QIODevice::ReadOnly));
        QCOMPARE(QString::fromLatin1(QCryptographicHash::hash(repaired.readAll(), QCryptographicHash::Sha256).toHex()), assetName.left(64));
        repaired.close();
        QCOMPARE(QDir(second.mediaPath()).entryList({QStringLiteral("*.damaged-*")}, QDir::Files).size(), 1);
        second.startReview(deckId); QVERIFY(waitIdle(second));
        QCOMPARE(second.queueCount(), 2);
        second.revealAnswer(); second.grade(1, 0.5); QVERIFY(waitIdle(second));
        QCOMPARE(second.history().size(), 1);
        QVERIFY2(synchronize(secondSync), qPrintable(secondSync.error()));
        QVERIFY2(synchronize(firstSync), qPrintable(firstSync.error()));
        QCOMPARE(first.history().size(), 1);
        const QString sourceId = first.cards().first().toMap().value(QStringLiteral("id")).toString();
        first.deleteCard(sourceId); QVERIFY(waitIdle(first));
        QVERIFY2(synchronize(firstSync), qPrintable(firstSync.error()));
        QVERIFY2(synchronize(secondSync), qPrintable(secondSync.error()));
        QCOMPARE(second.cards().size(), 0);
        QCOMPARE(second.history().size(), 1);
    }
    void cancelBeforeBatchReturnCanRestart() {
        LocalHttpFixture server([](const struct HttpRequest &request) {
            return HttpResponse{json(emptyResponse(request))};
        });
        QVERIFY(server.listen());
        AppController app(m_temporary.filePath(QStringLiteral("cancel-before-batch")));
        QVERIFY(waitIdle(app));
        SyncController sync(&app, app.mediaPath());
        sync.setEndpoint(server.endpoint());
        QSignalSpy batches(&app, &AppController::syncBatchReady);
        int completions = 0;
        connect(&sync, &SyncController::changed, this, [&] {
            if (!sync.busy() && sync.status() == QStringLiteral("Collection synced.")) ++completions;
        });
        sync.synchronize();
        QVERIFY(sync.busy());
        sync.cancel();
        sync.synchronize();
        QVERIFY(QTest::qWaitFor([&] { return !sync.busy() && !app.busy(); }, 10000));
        QVERIFY2(sync.error().isEmpty(), qPrintable(sync.error()));
        QCOMPARE(batches.size(), 2);
        QVERIFY(batches[0][0].toMap().value(QStringLiteral("requestId")) != batches[1][0].toMap().value(QStringLiteral("requestId")));
        QCOMPARE(server.requests.size(), 1);
        QCOMPARE(completions, 1);
        const QVariantMap outgoing = QJsonDocument::fromJson(server.requests.first().body).toVariant().toMap();
        QVERIFY(!outgoing.contains(QStringLiteral("requestId")));
    }
    void cancelAfterApplyDispatchDoesNotCompleteRestartedSync() {
        LocalHttpFixture server([](const struct HttpRequest &request) {
            QVariantMap response = emptyResponse(request);
            response.insert(QStringLiteral("requestId"), QStringLiteral("server-controlled-id"));
            return HttpResponse{json(response)};
        });
        QVERIFY(server.listen());
        AppController app(m_temporary.filePath(QStringLiteral("cancel-after-apply")));
        QVERIFY(waitIdle(app));
        SyncController sync(&app, app.mediaPath());
        sync.setEndpoint(server.endpoint());
        QSignalSpy applied(&app, &AppController::syncResponseApplied);
        bool canceled = false;
        int completions = 0;
        connect(&sync, &SyncController::changed, this, [&] {
            if (!canceled && sync.busy() && sync.status() == QStringLiteral("Applying received changes.")) {
                canceled = true;
                sync.cancel();
                sync.synchronize();
            }
            if (!sync.busy() && sync.status() == QStringLiteral("Collection synced.")) ++completions;
        });
        sync.synchronize();
        QVERIFY(QTest::qWaitFor([&] { return canceled && !sync.busy() && !app.busy(); }, 10000));
        QVERIFY2(sync.error().isEmpty(), qPrintable(sync.error()));
        QCOMPARE(server.requests.size(), 2);
        QCOMPARE(applied.size(), 2);
        QVERIFY(applied[0][0] != applied[1][0]);
        QVERIFY(applied[0][0].toString() != QStringLiteral("server-controlled-id"));
        QCOMPARE(completions, 1);
    }
    void localChangeDuringExchangeIsUploadedBeforeCompletion() {
        AppController app(m_temporary.filePath(QStringLiteral("delayed-loopback")));
        QVERIFY(waitIdle(app));
        app.createDeck(QStringLiteral("Delayed loopback deck"));
        QVERIFY(waitIdle(app));
        const QString deckId = app.decks().first().toMap().value(QStringLiteral("id")).toString();
        bool delayed = false;
        LocalHttpFixture server([&app, deckId, &delayed](const struct HttpRequest &request) {
            QVariantMap response = emptyResponse(request);
            const QVariantMap batch = QJsonDocument::fromJson(request.body).toVariant().toMap();
            QVariantList accepted;
            for (const QVariant &event : batch.value(QStringLiteral("events")).toList())
                accepted.append(event.toMap().value(QStringLiteral("id")));
            response.insert(QStringLiteral("acceptedIds"), accepted);
            if (request.method == QByteArray("POST") && !delayed) {
                delayed = true;
                QTimer::singleShot(20, &app, [&app, deckId] {
                    QVERIFY(app.saveCard({}, deckId, QStringLiteral("reverse"), QStringLiteral("Created during exchange"),
                        QStringLiteral("Answer"), {}, 1));
                });
                return HttpResponse{json(response), 200, 200};
            }
            return HttpResponse{json(response)};
        });
        QVERIFY(server.listen());
        SyncController sync(&app, app.mediaPath());
        sync.setEndpoint(server.endpoint());
        QVERIFY2(synchronize(sync), qPrintable(sync.error()));
        QCOMPARE(server.requests.size(), 2);
        const QVariantMap secondBatch = QJsonDocument::fromJson(server.requests.at(1).body).toVariant().toMap();
        const QVariantList secondEvents = secondBatch.value(QStringLiteral("events")).toList();
        QVERIFY(!secondEvents.isEmpty());
        bool uploadedCard = false;
        for (const QVariant &value : secondEvents)
            uploadedCard = uploadedCard || value.toMap().value(QStringLiteral("type")).toString() == QStringLiteral("card.upsert");
        QVERIFY(uploadedCard);
        QCOMPARE(app.cards().size(), 1);
    }
    void missingAcknowledgementsRetainOutboxForRetry() {
        bool acknowledge = false;
        LocalHttpFixture server([&acknowledge](const struct HttpRequest &request) {
            QVariantMap response = emptyResponse(request);
            if (acknowledge) {
                QVariantList accepted;
                const QVariantMap batch = QJsonDocument::fromJson(request.body).toVariant().toMap();
                for (const QVariant &event : batch.value(QStringLiteral("events")).toList())
                    accepted.append(event.toMap().value(QStringLiteral("id")));
                response.insert(QStringLiteral("acceptedIds"), accepted);
            }
            return HttpResponse{json(response)};
        });
        QVERIFY(server.listen());
        AppController app(m_temporary.filePath(QStringLiteral("missing-acknowledgements")));
        QVERIFY(waitIdle(app));
        app.createDeck(QStringLiteral("A pending local change"));
        QVERIFY(waitIdle(app));
        SyncController sync(&app, app.mediaPath());
        sync.setEndpoint(server.endpoint());
        sync.synchronize();
        QVERIFY(QTest::qWaitFor([&] { return !sync.busy(); }, 10000));
        QVERIFY(sync.error().startsWith(QStringLiteral("SYNC_ACKNOWLEDGEMENT")));
        QCOMPARE(server.requests.size(), 1);
        const QVariantList first = QJsonDocument::fromJson(server.requests[0].body).toVariant().toMap()
            .value(QStringLiteral("events")).toList();
        QCOMPARE(first.size(), 1);
        acknowledge = true;
        QVERIFY2(synchronize(sync), qPrintable(sync.error()));
        QCOMPARE(server.requests.size(), 2);
        const QVariantList retry = QJsonDocument::fromJson(server.requests[1].body).toVariant().toMap()
            .value(QStringLiteral("events")).toList();
        QCOMPARE(retry, first);
        QVERIFY2(synchronize(sync), qPrintable(sync.error()));
        QCOMPARE(server.requests.size(), 3);
        QVERIFY(QJsonDocument::fromJson(server.requests[2].body).toVariant().toMap().value(QStringLiteral("events")).toList().isEmpty());
        QCOMPARE(app.decks().size(), 1);
    }
    void malformedProtocolDoesNotFetchOrWriteImages_data() {
        QTest::addColumn<QVariantMap>("response");
        const QString name = imageName(smallImage());
        const QVariantMap valid = responseWithEvent(cardEvent(QStringLiteral("![diagram](media:%1)").arg(name)));
        QVariantMap badCursor = valid;
        badCursor.insert(QStringLiteral("cursor"), 2);
        QTest::newRow("cursor-gap") << badCursor;
        QVariantMap badAcknowledgement = valid;
        badAcknowledgement.insert(QStringLiteral("acceptedIds"), QVariantList{betterflash::model::uuid()});
        QTest::newRow("unsubmitted-acknowledgement") << badAcknowledgement;
        QVariantMap wrongType = valid;
        wrongType.insert(QStringLiteral("hasMore"), QStringLiteral("false"));
        QTest::newRow("field-type") << wrongType;
        QString manyImages;
        for (int i = 0; i < 65; ++i) manyImages += QStringLiteral("![diagram](media:%1)\n").arg(imageName(QByteArray::number(i)));
        QTest::newRow("too-many-images") << responseWithEvent(cardEvent(manyImages));
        QVariantMap malformedCard = cardEvent(QStringLiteral("![diagram](media:%1)").arg(name));
        QVariantMap payload = malformedCard.value(QStringLiteral("payload")).toMap();
        payload.insert(QStringLiteral("variants"), QVariantList{});
        malformedCard.insert(QStringLiteral("payload"), payload);
        QTest::newRow("invalid-card-event") << responseWithEvent(malformedCard);
    }
    void malformedProtocolDoesNotFetchOrWriteImages() {
        QFETCH(QVariantMap, response);
        LocalHttpFixture server([response](const struct HttpRequest &) { return HttpResponse{json(response)}; });
        QVERIFY(server.listen());
        const QString directory = m_temporary.filePath(QStringLiteral("malformed-") + QString::fromLatin1(QTest::currentDataTag()));
        AppController app(directory);
        QVERIFY(waitIdle(app));
        SyncController sync(&app, app.mediaPath());
        sync.setEndpoint(server.endpoint());
        sync.synchronize();
        QVERIFY(QTest::qWaitFor([&] { return !sync.busy(); }, 10000));
        QVERIFY(!sync.error().isEmpty());
        QCOMPARE(server.requests.size(), 1);
        QCOMPARE(server.requests.first().method, QByteArray("POST"));
        QVERIFY(QDir(app.mediaPath()).entryList(QDir::Files).isEmpty());
        QVERIFY(app.cards().isEmpty());
    }
    void mediaReferencesOutsideCardSidesDoNotFetchImages() {
        const QString name = imageName(smallImage());
        const struct betterflash::model::Deck deck{betterflash::model::uuid(), QStringLiteral("Metadata references"),
            QStringLiteral("media:%1").arg(name), betterflash::model::nowUtc()};
        const QVariantMap event{{QStringLiteral("id"), betterflash::model::uuid()},
            {QStringLiteral("deviceId"), betterflash::model::uuid()}, {QStringLiteral("type"), QStringLiteral("deck.upsert")},
            {QStringLiteral("createdAt"), betterflash::model::nowUtc()},
            {QStringLiteral("payload"), QVariantMap{{QStringLiteral("deck"), betterflash::model::toMap(deck)}}}};
        QVariantMap response = responseWithEvent(event);
        response.insert(QStringLiteral("media"), QVariantList{name});
        LocalHttpFixture server([response](const struct HttpRequest &) { return HttpResponse{json(response)}; });
        QVERIFY(server.listen());
        AppController app(m_temporary.filePath(QStringLiteral("metadata-media")));
        QVERIFY(waitIdle(app));
        SyncController sync(&app, app.mediaPath());
        sync.setEndpoint(server.endpoint());
        QVERIFY2(synchronize(sync), qPrintable(sync.error()));
        QCOMPARE(server.requests.size(), 1);
        QVERIFY(QDir(app.mediaPath()).entryList(QDir::Files).isEmpty());
        QCOMPARE(app.decks().size(), 1);
    }
    void imageDecodeValidationPrecedesPublishing_data() {
        QTest::addColumn<QByteArray>("bytes");
        QTest::addColumn<QString>("name");
        const QByteArray nonImage("a hash alone does not make an image");
        QTest::newRow("hash-valid-non-image") << nonImage << imageName(nonImage);
        const QByteArray image = smallImage();
        QTest::newRow("extension-mismatch") << image << imageName(image).replace(QStringLiteral(".png"), QStringLiteral(".gif"));
        QTest::newRow("hash-mismatch") << image << imageName(nonImage);
        const QByteArray tooLarge(betterflash::model::maxImageBytes + 1, 'x');
        QTest::newRow("byte-limit") << tooLarge << imageName(tooLarge);
    }
    void imageDecodeValidationPrecedesPublishing() {
        QFETCH(QByteArray, bytes);
        QFETCH(QString, name);
        const QVariantMap response = responseWithEvent(cardEvent(QStringLiteral("![diagram](media:%1)").arg(name)));
        LocalHttpFixture server([response, bytes](const struct HttpRequest &request) {
            return HttpResponse{request.method == "POST" ? json(response) : bytes};
        });
        QVERIFY(server.listen());
        AppController app(m_temporary.filePath(QStringLiteral("invalid-download-") + QString::fromLatin1(QTest::currentDataTag())));
        QVERIFY(waitIdle(app));
        SyncController sync(&app, app.mediaPath());
        sync.setEndpoint(server.endpoint());
        sync.synchronize();
        QVERIFY(QTest::qWaitFor([&] { return !sync.busy(); }, 10000));
        QVERIFY(sync.error().startsWith(QStringLiteral("SYNC_IMAGE_")));
        QCOMPARE(server.requests.size(), 2);
        QVERIFY(QDir(app.mediaPath()).entryList(QDir::Files).isEmpty());
        QVERIFY(app.cards().isEmpty());
    }
    void imagePageBudgetStopsBeforeCollectionApply() {
        const QByteArray image = smallImage();
        QMap<QString, QByteArray> assets;
        QString front;
        for (int i = 0; i < 4; ++i) {
            QByteArray bytes = image;
            bytes.append(QByteArray(17 * 1024 * 1024 - bytes.size(), static_cast<char>('a' + i)));
            const QString name = imageName(bytes);
            QVERIFY(MediaStore::validateImage(name, bytes).has_value());
            assets.insert(name, bytes);
            front += QStringLiteral("![diagram](media:%1)\n").arg(name);
        }
        const QVariantMap response = responseWithEvent(cardEvent(front));
        LocalHttpFixture server([response, assets](const struct HttpRequest &request) {
            const QString name = QString::fromUtf8(request.path).section(QLatin1Char('/'), -1);
            return HttpResponse{request.method == "POST" ? json(response) : assets.value(name)};
        });
        QVERIFY(server.listen());
        AppController app(m_temporary.filePath(QStringLiteral("aggregate-download-limit")));
        QVERIFY(waitIdle(app));
        SyncController sync(&app, app.mediaPath());
        sync.setEndpoint(server.endpoint());
        sync.synchronize();
        QVERIFY(QTest::qWaitFor([&] { return !sync.busy(); }, 20000));
        QVERIFY(sync.error().startsWith(QStringLiteral("SYNC_IMAGE_")));
        QCOMPARE(server.requests.size(), 5);
        QVERIFY(QDir(app.mediaPath()).entryList(QDir::Files).size() <= 3);
        QVERIFY(app.cards().isEmpty());
    }
    void keyboardBindingsAreValidatedAndPersisted() {
        ShortcutManager bindings;
        QVERIFY(!bindings.rebind(QStringLiteral("newCard"), bindings.get(QStringLiteral("commandPalette"))));
        QVERIFY(!bindings.lastError().isEmpty());
        QVERIFY(!bindings.rebind(QStringLiteral("newCard"), QStringLiteral("Esc")));
        QVERIFY(bindings.rebind(QStringLiteral("newCard"), QStringLiteral("Ctrl+Alt+N")));
        ShortcutManager reloaded;
        QCOMPARE(reloaded.get(QStringLiteral("newCard")), QStringLiteral("Ctrl+Alt+N"));
        reloaded.reset();
        QVERIFY(!reloaded.customized());
    }
    void mediaImporterRejectsNonImages() {
        MediaStore media(m_temporary.filePath(QStringLiteral("invalid-images")));
        const QString input = m_temporary.filePath(QStringLiteral("text.png"));
        QFile output(input); QVERIFY(output.open(QIODevice::WriteOnly));
        output.write("This is not an image."); output.close();
        media.importImage(QUrl::fromLocalFile(input));
        QVERIFY(QTest::qWaitFor([&media] { return !media.busy(); }, 5000));
        QVERIFY(media.error().startsWith(QStringLiteral("IMAGE_FORMAT")));
        QCOMPARE(QDir(media.rootPath()).entryList(QDir::Files).size(), 0);
    }
    void mediaImporterRejectsLinkedRootsAndCorruptExistingFiles() {
        const QString data = m_temporary.filePath(QStringLiteral("linked-root"));
        const QString outside = m_temporary.filePath(QStringLiteral("outside"));
        QVERIFY(QDir().mkpath(data)); QVERIFY(QDir().mkpath(outside));
        QVERIFY(QFile::link(outside, QDir(data).filePath(QStringLiteral("media"))));
        QImage image(40, 20, QImage::Format_RGB32); image.fill(Qt::green);
        const QString input = m_temporary.filePath(QStringLiteral("symlink-fixture.png"));
        QVERIFY(image.save(input));
        MediaStore linked(data); linked.importImage(QUrl::fromLocalFile(input));
        QVERIFY(QTest::qWaitFor([&linked] { return !linked.busy(); }, 5000));
        QVERIFY(linked.error().startsWith(QStringLiteral("IMAGE_STORAGE")));
        QVERIFY(QDir(outside).entryList(QDir::Files).isEmpty());
        MediaStore normal(m_temporary.filePath(QStringLiteral("corrupt-existing")));
        QSignalSpy imported(&normal, &MediaStore::imageImported);
        normal.importImage(QUrl::fromLocalFile(input)); QVERIFY(imported.wait(5000));
        const QString name = QDir(normal.rootPath()).entryList(QDir::Files).first();
        QFile corrupt(QDir(normal.rootPath()).filePath(name)); QVERIFY(corrupt.open(QIODevice::WriteOnly | QIODevice::Truncate));
        corrupt.write("bad bytes"); corrupt.close();
        normal.importImage(QUrl::fromLocalFile(input));
        QVERIFY(QTest::qWaitFor([&normal] { return !normal.busy(); }, 5000));
        QVERIFY(normal.error().startsWith(QStringLiteral("IMAGE_STORAGE")));
        QCOMPARE(imported.size(), 1);
    }
    void cleanupTestCase() {
        m_server.terminate();
        if (!m_server.waitForFinished(3000)) { m_server.kill(); m_server.waitForFinished(3000); }
        qunsetenv("BETTERFLASH_SYNC_TOKEN");
    }
};

QTEST_MAIN(IntegrationTest)
#include "integration_test.moc"
