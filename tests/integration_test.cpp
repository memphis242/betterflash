#include "core/appcontroller.h"
#include "core/cardmodel.h"
#include "core/mediabackup.h"
#include "core/schedulelimits.h"
#include "core/syncprotocol.h"
#include "integrations/MediaStore.h"
#include "integrations/ShortcutManager.h"
#include "integrations/SyncController.h"
#include <QBuffer>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QImage>
#include <QJsonDocument>
#include <QProcess>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QSettings>
#include <QSignalSpy>
#include <QSqlDatabase>
#include <QSqlQuery>
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
    explicit LocalHttpFixture(Handler handler,
                              QString collectionId = QStringLiteral("11111111-1111-4111-8111-111111111111"),
                              const int protocolVersion = 4)
        : m_handler(std::move(handler)), m_collectionId(std::move(collectionId)), m_protocolVersion(protocolVersion) {
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
                    if (request.method == QByteArrayLiteral("GET") && request.path.endsWith("/v1/info")) {
                        ++infoRequests;
                        const QByteArray body = QJsonDocument::fromVariant(QVariantMap{
                            {QStringLiteral("collectionId"), m_collectionId},
                            {QStringLiteral("protocolVersion"), m_protocolVersion},
                            {QStringLiteral("capabilities"), m_protocolVersion >= 4
                                 ? QVariantList{QStringLiteral("independent-schedules"), QStringLiteral("hierarchy-repair"), QStringLiteral("short-interval-schedules")}
                                 : QVariantList{QStringLiteral("independent-schedules"), QStringLiteral("hierarchy-repair")}}}).toJson(QJsonDocument::Compact);
                        socket->write("HTTP/1.1 200 Fixture\r\nContent-Length: " + QByteArray::number(body.size())
                                      + "\r\nConnection: close\r\n\r\n" + body);
                        socket->disconnectFromHost();
                        return;
                    }
                    requests.append(request);
                    struct HttpResponse response = m_handler(request);
                    if (request.method == QByteArrayLiteral("POST")) {
                        QJsonParseError parseError;
                        QJsonDocument document = QJsonDocument::fromJson(response.body, &parseError);
                        if (parseError.error == QJsonParseError::NoError && document.isObject()) {
                            QJsonObject object = document.object();
                            if (!object.contains(QStringLiteral("collectionId"))) object.insert(QStringLiteral("collectionId"), m_collectionId);
                            response.body = QJsonDocument(object).toJson(QJsonDocument::Compact);
                        }
                    }
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
    int infoRequests = 0;
private:
    QTcpServer m_server;
    const Handler m_handler;
    const QString m_collectionId;
    const int m_protocolVersion;
    QHash<QTcpSocket *, QByteArray> m_buffers;
};
void configureSync(SyncController &sync, const QString &endpoint) {
    sync.setEndpoint(endpoint);
    if (!sync.hasToken()) sync.setToken(QStringLiteral("local-integration-test-token-0123456789"));
}
QByteArray json(const QVariantMap &value) { return QJsonDocument::fromVariant(value).toJson(QJsonDocument::Compact); }
QVariantMap emptyResponse(const struct HttpRequest &request) {
    const QVariantMap batch = QJsonDocument::fromJson(request.body).toVariant().toMap();
    return {{QStringLiteral("acceptedIds"), QVariantList{}}, {QStringLiteral("events"), QVariantList{}},
            {QStringLiteral("cursor"), batch.value(QStringLiteral("cursor"))}, {QStringLiteral("hasMore"), false},
            {QStringLiteral("collectionId"), QStringLiteral("11111111-1111-4111-8111-111111111111")}};
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
        {QStringLiteral("cursor"), 1}, {QStringLiteral("hasMore"), false},
        {QStringLiteral("collectionId"), QStringLiteral("11111111-1111-4111-8111-111111111111")}};
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
struct DatabaseSnapshot {
    bool valid = false;
    QString front;
    QString reviewedAt;
    QVariantMap forward;
    QVariantMap reverse;
    int historyCount = 0;
};
struct DatabaseSnapshot readSnapshot(const QString &path, const QString &cardId) {
    const QString connection = betterflash::model::uuid();
    struct DatabaseSnapshot snapshot;
    {
        QSqlDatabase database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connection);
        database.setDatabaseName(path);
        database.setConnectOptions(QStringLiteral("QSQLITE_OPEN_READONLY"));
        const auto read = [&]() -> struct DatabaseSnapshot {
            struct DatabaseSnapshot result;
            if (!database.open()) return result;
            QSqlQuery card(database);
            card.prepare(QStringLiteral("SELECT front FROM notes WHERE id=?"));
            card.addBindValue(cardId);
            if (!card.exec() || !card.next()) return result;
            result.front = card.value(0).toString();
            QSqlQuery variants(database);
            variants.prepare(QStringLiteral("SELECT variant_key,due,review_count,stability,difficulty,graduated FROM review_variants WHERE card_id=? ORDER BY variant_key"));
            variants.addBindValue(cardId);
            if (!variants.exec()) return result;
            while (variants.next()) {
                const QVariantMap value{{QStringLiteral("due"), variants.value(1)}, {QStringLiteral("reviewCount"), variants.value(2)},
                    {QStringLiteral("stability"), variants.value(3)}, {QStringLiteral("difficulty"), variants.value(4)},
                    {QStringLiteral("graduated"), variants.value(5)}};
                if (variants.value(0).toString() == QStringLiteral("forward")) result.forward = value;
                else if (variants.value(0).toString() == QStringLiteral("reverse")) result.reverse = value;
            }
            QSqlQuery history(database);
            history.prepare(QStringLiteral("SELECT COUNT(*) FROM reviews WHERE card_id=?"));
            history.addBindValue(cardId);
            if (!history.exec() || !history.next()) return result;
            result.historyCount = history.value(0).toInt();
            QSqlQuery latestReview(database);
            latestReview.prepare(QStringLiteral("SELECT reviewed_at FROM reviews WHERE card_id=? ORDER BY reviewed_at DESC,id DESC LIMIT 1"));
            latestReview.addBindValue(cardId);
            if (!latestReview.exec()) return result;
            if (latestReview.next()) result.reviewedAt = latestReview.value(0).toString();
            result.valid = true;
            return result;
        };
        snapshot = read();
        database.close();
    }
    QSqlDatabase::removeDatabase(connection);
    return snapshot;
}
QVariantMap deckParents(const AppController &app) {
    QVariantMap parents;
    for (const QVariant &value : app.decks()) {
        const QVariantMap deck = value.toMap();
        parents.insert(deck.value(QStringLiteral("id")).toString(), deck.value(QStringLiteral("parentId")).toString());
    }
    return parents;
}
int outboxCount(const QString &path) {
    const QString connection = betterflash::model::uuid();
    int count = -1;
    {
        QSqlDatabase database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connection);
        database.setDatabaseName(path);
        database.setConnectOptions(QStringLiteral("QSQLITE_OPEN_READONLY"));
        if (database.open()) {
            QSqlQuery query(database);
            if (query.exec(QStringLiteral("SELECT COUNT(*) FROM sync_outbox")) && query.next()) count = query.value(0).toInt();
        }
        database.close();
    }
    QSqlDatabase::removeDatabase(connection);
    return count;
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
    bool synchronizeAllowHierarchyResolution(SyncController &sync) {
        sync.synchronize();
        if (!QTest::qWaitFor([&sync] { return !sync.busy(); }, 30000)) return false;
        return sync.error().isEmpty() || sync.error().startsWith(QStringLiteral("SYNC_HIERARCHY_RESOLVED"));
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
        environment.insert(QStringLiteral("PYTHONDONTWRITEBYTECODE"), QStringLiteral("1"));
        environment.remove(QStringLiteral("GROQ_API_KEY"));
        environment.remove(QStringLiteral("BETTERFLASH_LLM_API_KEY"));
        m_server.setProcessEnvironment(environment);
    }
    void init() {
        m_server.start(QStringLiteral("python3"), {QStringLiteral(BETTERFLASH_SOURCE_DIR "/server/main.py"),
            QStringLiteral("--port"), QStringLiteral("0"), QStringLiteral("--database"),
            m_temporary.filePath(QStringLiteral("server-%1.sqlite").arg(betterflash::model::uuid())),
            QStringLiteral("--media"), m_temporary.filePath(QStringLiteral("server-media-%1").arg(betterflash::model::uuid()))});
        QVERIFY(m_server.waitForStarted(5000));
        QVERIFY(m_server.waitForReadyRead(5000));
        const QString message = QString::fromUtf8(m_server.readAllStandardOutput());
        const auto match = QRegularExpression(QStringLiteral("http://127\\.0\\.0\\.1:[0-9]+")).match(message);
        QVERIFY(match.hasMatch());
        m_endpoint = match.captured() + QStringLiteral("/v1/sync");
        qputenv("BETTERFLASH_SYNC_ENDPOINT", m_endpoint.toUtf8());
    }
    void environmentTokenIsNeverSentToAnotherEndpoint() {
        LocalHttpFixture other([](const struct HttpRequest &request) { return HttpResponse{json(emptyResponse(request))}; });
        QVERIFY(other.listen());
        AppController app(m_temporary.filePath(QStringLiteral("scoped-environment-token")));
        QVERIFY(waitIdle(app));
        SyncController sync(&app, app.mediaPath());
        sync.setEndpoint(m_endpoint);
        QVERIFY(sync.hasToken());
        QCOMPARE(sync.keyStorageState(), QStringLiteral("environment"));
        sync.setEndpoint(other.endpoint());
        sync.synchronize();
        QVERIFY(QTest::qWaitFor([&sync] { return !sync.busy(); }, 5000));
        QCOMPARE(other.infoRequests, 0);
        QCOMPARE(other.requests.size(), 0);
        QVERIFY(sync.error().startsWith(QStringLiteral("SYNC_TOKEN")));
        QVERIFY(!sync.hasToken());
    }
    void durableNoticesSurviveCleanSyncAndConnectionFailure() {
        const QString directory = m_temporary.filePath(QStringLiteral("durable-controller-notices"));
        QVariantMap retained;
        QString deckId;
        {
            AppController app(directory);
            AppController peer(m_temporary.filePath(QStringLiteral("durable-controller-peer")));
            QVERIFY(waitIdle(app)); QVERIFY(waitIdle(peer));
            app.createDeck(QStringLiteral("Shared deck")); QVERIFY(waitIdle(app));
            deckId = app.decks().first().toMap().value(QStringLiteral("id")).toString();
            SyncController sync(&app, app.mediaPath()), peerSync(&peer, peer.mediaPath());
            configureSync(sync, m_endpoint); configureSync(peerSync, m_endpoint);
            QVERIFY2(synchronize(sync), qPrintable(sync.error()));
            QVERIFY2(synchronize(peerSync), qPrintable(peerSync.error()));
            app.updateDeck(deckId, QStringLiteral("Desktop edit"));
            peer.updateDeck(deckId, QStringLiteral("Peer edit"));
            QVERIFY(waitIdle(app)); QVERIFY(waitIdle(peer));
            QVERIFY2(synchronize(peerSync), qPrintable(peerSync.error()));
            sync.synchronize();
            QVERIFY(QTest::qWaitFor([&sync] { return !sync.busy(); }, 10000));
            QVERIFY2(sync.error().startsWith(QStringLiteral("SYNC_CONTENT_CONFLICT")), qPrintable(sync.error()));
            retained = sync.syncNotices();
            QVERIFY(retained.value(QStringLiteral("contentConflictCount")).toLongLong() > 0);
            QVERIFY(betterflash::model::validUuid(retained.value(QStringLiteral("revision")).toString()));
            QVERIFY(retained.value(QStringLiteral("records")).toStringList().contains(QStringLiteral("deck ") + deckId));
            sync.synchronize();
            QVERIFY(QTest::qWaitFor([&sync] { return !sync.busy(); }, 10000));
            QCOMPARE(QJsonObject::fromVariantMap(sync.syncNotices()), QJsonObject::fromVariantMap(retained));
            QVERIFY(sync.error().startsWith(QStringLiteral("SYNC_CONTENT_CONFLICT")));
        }
        AppController restarted(directory); QVERIFY(waitIdle(restarted));
        SyncController sync(&restarted, restarted.mediaPath());
        configureSync(sync, m_endpoint);
        sync.synchronize();
        QVERIFY(QTest::qWaitFor([&sync] { return !sync.busy(); }, 10000));
        QCOMPARE(QJsonObject::fromVariantMap(sync.syncNotices()), QJsonObject::fromVariantMap(retained));
        QVERIFY(sync.error().startsWith(QStringLiteral("SYNC_CONTENT_CONFLICT")));

        QTcpServer unavailable;
        QVERIFY(unavailable.listen(QHostAddress::LocalHost, 0));
        const QString unreachableEndpoint = QStringLiteral("http://127.0.0.1:%1/v1/sync").arg(unavailable.serverPort());
        unavailable.close();
        configureSync(sync, unreachableEndpoint);
        sync.synchronize();
        QVERIFY(QTest::qWaitFor([&sync] { return !sync.busy(); }, 10000));
        QVERIFY(sync.error().startsWith(QStringLiteral("SYNC_IDENTITY")));
        QVERIFY(sync.error().contains(QStringLiteral("SYNC_CONTENT_CONFLICT")));
        QCOMPARE(QJsonObject::fromVariantMap(sync.syncNotices()), QJsonObject::fromVariantMap(retained));

        configureSync(sync, m_endpoint);
        sync.synchronize();
        QVERIFY(QTest::qWaitFor([&sync] { return !sync.busy(); }, 10000));
        QCOMPARE(QJsonObject::fromVariantMap(sync.syncNotices()), QJsonObject::fromVariantMap(retained));
        const int pending = outboxCount(restarted.storagePath());
        restarted.acknowledgeSyncNotices(retained.value(QStringLiteral("revision")).toString());
        QVERIFY(waitIdle(restarted));
        QCOMPARE(sync.syncNotices().value(QStringLiteral("contentConflictCount")).toLongLong(), qint64(0));
        QVERIFY(sync.syncNotices().value(QStringLiteral("records")).toStringList().isEmpty());
        QVERIFY(sync.syncNotices().value(QStringLiteral("revision")) != retained.value(QStringLiteral("revision")));
        QVERIFY(sync.error().isEmpty());
        QCOMPARE(outboxCount(restarted.storagePath()), pending);
        QVERIFY2(synchronize(sync), qPrintable(sync.error()));
    }
    QVariantMap prepare(AppController &app) {
        QSignalSpy ready(&app, &AppController::syncBatchReady);
        app.requestSyncBatch();
        if (ready.isEmpty() && !ready.wait(5000)) return {};
        return ready.first().first().toMap();
    }
    QVariantMap upload(const QVariantMap &batch) {
        QNetworkAccessManager network;
        QNetworkRequest request{QUrl(m_endpoint)};
        request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
        request.setRawHeader("Authorization", "Bearer local-integration-test-token-0123456789");
        request.setRawHeader("X-BetterFlash-Protocol", "4");
        QVariantMap outgoing = batch;
        if (outgoing.value("collectionId").toString().isEmpty()) outgoing.remove("collectionId");
        QNetworkReply *const reply = network.post(request, json(outgoing));
        if (!QTest::qWaitFor([reply] { return reply->isFinished(); }, 10000)) return {};
        QVariantMap result = QJsonDocument::fromJson(reply->readAll()).toVariant().toMap();
        if (result.contains("error")) qWarning() << result;
        result.insert("requestId", batch.value("requestId"));
        return result;
    }
    bool receive(AppController &app, const QVariantMap &response) {
        QSignalSpy applied(&app, &AppController::syncApplied);
        app.applySyncResponse(response);
        if (applied.isEmpty() && !applied.wait(5000)) return false;
        return QTest::qWaitFor([&app] { return !app.busy(); }, 5000) && applied.first().first().toBool();
    }
    bool exchange(AppController &app) { return receive(app, upload(prepare(app))); }
    bool exchangeOneEventPages(AppController &app) {
        bool more = true;
        for (int page=0; more && page<100; ++page) {
            QVariantMap result = upload(prepare(app));
            const QVariantList pageEvents = result.value("events").toList();
            if (pageEvents.size() > 1) {
                const QVariantMap first = pageEvents.first().toMap();
                const qint64 seq = first.value("seq").toLongLong();
                QVariantList pageConflicts;
                for (const QVariant &value : result.value("contentConflicts").toList())
                    if (value.toMap().value("seq").toLongLong() == seq) pageConflicts.append(value);
                result.insert("events", QVariantList{first}); result.insert("cursor", seq); result.insert("hasMore", true);
                result.insert("contentConflicts", pageConflicts); result.insert("contentConflictCount", pageConflicts.size());
                result.insert("hierarchyResolutionCount", 0);
            }
            more = result.value("hasMore").toBool();
            if (!receive(app,result)) return false;
        }
        return !more;
    }
    QString create(AppController &app, const QString &name) {
        QSet<QString> before;
        for (const QVariant &deck : app.decks()) before.insert(deck.toMap().value("id").toString());
        app.createDeck(name);
        if (!waitIdle(app)) return {};
        for (const QVariant &deck : app.decks())
            if (!before.contains(deck.toMap().value("id").toString())) return deck.toMap().value("id").toString();
        return {};
    }
    QVariantList schedules(const AppController &app) {
        return app.cards().isEmpty() ? QVariantList{} : app.cards().first().toMap().value("variants").toList();
    }
    void variantBoundsAcceptShortIntervalsAndRejectInvalidValues() {
        const QString cardId = QStringLiteral("11111111-1111-4111-8111-111111111111");
        QVariantMap variant{{QStringLiteral("id"), betterflash::model::variantId(cardId, QStringLiteral("forward"))},
                            {QStringLiteral("cardId"), cardId}, {QStringLiteral("key"), QStringLiteral("forward")},
                            {QStringLiteral("due"), QStringLiteral("2026-11-08T00:10:00Z")},
                            {QStringLiteral("reviewCount"), 0}, {QStringLiteral("difficulty"), 0.5}};
        for (const double stability : {10.0 / 1440.0, 1.0 / 24.0, 1.0 / 3.0, 1.0}) {
            variant.insert(QStringLiteral("stability"), stability);
            QVERIFY(betterflash::model::variantFromMap(variant).has_value());
        }
        variant.insert(QStringLiteral("stability"), betterflash::scheduler::minimumStabilityDays / 2.0);
        QVERIFY(!betterflash::model::variantFromMap(variant).has_value());
        variant.insert(QStringLiteral("stability"), betterflash::scheduler::maximumStabilityDays + 0.001);
        QVERIFY(!betterflash::model::variantFromMap(variant).has_value());
        variant.insert(QStringLiteral("reviewCount"), 3);
        variant.insert(QStringLiteral("stability"), 0.25);
        variant.remove(QStringLiteral("graduated"));
        const auto legacy = betterflash::model::variantFromMap(variant);
        QVERIFY(legacy.has_value());
        QVERIFY(legacy->graduated);
        QCOMPARE(legacy->stability, 0.25);
        QCOMPARE(legacy->due, QStringLiteral("2026-11-08T00:10:00.000Z"));
        QVERIFY(betterflash::model::toMap(*legacy).value(QStringLiteral("graduated")).toBool());
        variant.insert(QStringLiteral("graduated"), false);
        const auto explicitlyYoung = betterflash::model::variantFromMap(variant);
        QVERIFY(explicitlyYoung.has_value());
        QVERIFY(!explicitlyYoung->graduated);
        variant.insert(QStringLiteral("graduated"), QStringLiteral("true"));
        QVERIFY(!betterflash::model::variantFromMap(variant).has_value());
        variant.insert(QStringLiteral("graduated"), false);
        variant.insert(QStringLiteral("reviewCount"), 1);
        variant.insert(QStringLiteral("stability"), 1.0);
        QVERIFY(!betterflash::model::variantFromMap(variant).has_value());
        variant.insert(QStringLiteral("graduated"), true);
        variant.insert(QStringLiteral("reviewCount"), 0);
        QVERIFY(!betterflash::model::variantFromMap(variant).has_value());
        variant.insert(QStringLiteral("graduated"), false);
        QVERIFY(betterflash::model::variantFromMap(variant).has_value());
    }
    void backupVersionsRetainCheckpointsAndShortIntervals() {
        const QString deckId = QStringLiteral("11111111-1111-4111-8111-111111111111");
        const QString cardId = QStringLiteral("22222222-2222-4222-8222-222222222222");
        const struct betterflash::model::Deck deck{deckId, QStringLiteral("Deck"), {}, QStringLiteral("2026-11-08T00:00:00Z"), {}};
        const struct betterflash::model::Card card{cardId, deckId, QStringLiteral("basic"),
            QStringLiteral("Question"), QStringLiteral("Answer"), {}, 1, {}, {}};
        const auto collection = [&](const int version, const double stability, const bool checkpoints, const bool includePhase = true) {
            const struct betterflash::model::Variant variant{betterflash::model::variantId(cardId, QStringLiteral("forward")),
                cardId, QStringLiteral("forward"), QStringLiteral("2026-11-08T00:10:00Z"), version == 5 ? 3 : 1, stability, 0.5};
            QVariantMap variantMap = betterflash::model::toMap(variant);
            if (version < 6 || !includePhase) variantMap.remove(QStringLiteral("graduated"));
            QJsonObject value{{QStringLiteral("version"), version},
                {QStringLiteral("decks"), QJsonArray::fromVariantList({betterflash::model::toMap(deck)})},
                {QStringLiteral("cards"), QJsonArray::fromVariantList({betterflash::model::toMap(card)})},
                {QStringLiteral("variants"), QJsonArray::fromVariantList({variantMap})},
                {QStringLiteral("history"), QJsonArray{}}};
            if (checkpoints) value.insert(QStringLiteral("checkpoints"), QJsonArray{});
            return betterflash::model::collectionFromJson(value);
        };
        const auto oldBackup = collection(5, 0.25, true);
        QVERIFY(oldBackup.has_value());
        QVERIFY(oldBackup->variants.first().graduated);
        QCOMPARE(oldBackup->variants.first().stability, 0.25);
        QCOMPARE(oldBackup->variants.first().due, QStringLiteral("2026-11-08T00:10:00.000Z"));
        const auto currentBackup = collection(6, betterflash::scheduler::minimumStabilityDays, false);
        QVERIFY(currentBackup.has_value());
        QVERIFY(!currentBackup->variants.first().graduated);
        QVERIFY(!collection(6, betterflash::scheduler::minimumStabilityDays, false, false).has_value());
    }
    void authoritativeDeletionSurvivesMoveOutAndFreshReplay() {
        AppController desktop(m_temporary.filePath("scope-desktop")), phone(m_temporary.filePath("scope-phone"));
        QVERIFY(waitIdle(desktop)); QVERIFY(waitIdle(phone));
        const QString parent = create(desktop, "Parent"), child = create(desktop, "Child");
        QVERIFY(desktop.saveCard({}, child, "basic", "Question", "Answer", {}, 1)); QVERIFY(waitIdle(desktop));
        QVERIFY(exchange(desktop)); QVERIFY(exchange(phone));
        desktop.deleteDeck(parent); QVERIFY(waitIdle(desktop));
        QVERIFY(phone.moveDeck(child, parent)); QVERIFY(waitIdle(phone)); QVERIFY(exchange(phone));
        QVERIFY(exchange(desktop));
        QVERIFY(phone.moveDeck(child, {})); QVERIFY(waitIdle(phone)); QVERIFY(exchange(phone));
        QVERIFY(exchange(desktop));
        AppController fresh(m_temporary.filePath("scope-fresh")); QVERIFY(waitIdle(fresh)); QVERIFY(exchange(fresh));
        QCOMPARE(deckParents(phone), deckParents(fresh));
        QCOMPARE(deckParents(desktop), deckParents(fresh));
        QCOMPARE(phone.cards().size(), fresh.cards().size());
        QVERIFY(fresh.decks().isEmpty()); QVERIFY(fresh.cards().isEmpty());
        AppController paged(m_temporary.filePath("scope-paged")); QVERIFY(waitIdle(paged));
        bool more=true;
        for (int page=0;more&&page<100;++page) {
            QVariantMap result=upload(prepare(paged));
            const QVariantList events=result.value("events").toList();
            if (events.size()>1) {
                const QVariantMap wrapper=events.first().toMap();
                result.insert("events",QVariantList{wrapper}); result.insert("cursor",wrapper.value("seq")); result.insert("hasMore",true);
                result.remove("contentConflicts"); result.remove("contentConflictCount"); result.remove("hierarchyResolutionCount");
            }
            more=result.value("hasMore").toBool(); QVERIFY(receive(paged,result));
        }
        QVERIFY(!more); QCOMPARE(deckParents(paged),deckParents(fresh)); QCOMPARE(paged.cards().size(),fresh.cards().size());
    }
    void movingCardThroughDeletedDeckConvergesAcrossPages_data() {
        QTest::addColumn<bool>("moveOut");
        QTest::newRow("moves-back-to-live-deck") << true;
        QTest::newRow("ends-in-deleted-deck") << false;
    }
    void movingCardThroughDeletedDeckConvergesAcrossPages() {
        QFETCH(bool,moveOut);
        const QString suffix=QString::fromLatin1(QTest::currentDataTag());
        AppController author(m_temporary.filePath("transit-author-"+suffix)), deleting(m_temporary.filePath("transit-deleting-"+suffix));
        QVERIFY(waitIdle(author)); QVERIFY(waitIdle(deleting));
        const QString parent=create(author,"Deleted parent"),other=create(author,"Other deck");
        QVERIFY(author.saveCard({},other,"basic","Question","Answer",{},1)); QVERIFY(waitIdle(author));
        const QString id=author.cards().first().toMap().value("id").toString();
        QVERIFY(exchange(author)); QVERIFY(exchange(deleting));
        deleting.deleteDeck(parent); QVERIFY(waitIdle(deleting)); QVERIFY(exchange(deleting));
        QVERIFY(author.moveCards(QVariantList{id},parent)); QVERIFY(waitIdle(author));
        if (moveOut) {QVERIFY(author.moveCards(QVariantList{id},other)); QVERIFY(waitIdle(author));}
        QVERIFY(exchange(author));
        AppController together(m_temporary.filePath("transit-together-"+suffix)),paged(m_temporary.filePath("transit-paged-"+suffix));
        QVERIFY(waitIdle(together)); QVERIFY(waitIdle(paged)); QVERIFY(exchange(together));
        bool more=true;
        for (int page=0;more&&page<100;++page) {
            QVariantMap result=upload(prepare(paged));
            const QVariantList events=result.value("events").toList();
            if (events.size()>1) {
                const QVariantMap wrapper=events.first().toMap();
                result.insert("events",QVariantList{wrapper}); result.insert("cursor",wrapper.value("seq")); result.insert("hasMore",true);
                result.remove("contentConflicts"); result.remove("contentConflictCount"); result.remove("hierarchyResolutionCount");
            }
            more=result.value("hasMore").toBool(); QVERIFY(receive(paged,result));
        }
        QVERIFY(!more); QVERIFY(exchange(deleting));
        QCOMPARE(together.cards().size(),paged.cards().size());
        QCOMPARE(author.cards().size(),paged.cards().size());
        QCOMPARE(deleting.cards().size(),paged.cards().size());
        QVERIFY(paged.cards().isEmpty());
        for (const QString &path:{author.storagePath(),deleting.storagePath(),together.storagePath(),paged.storagePath()}) {
            const QString connection=betterflash::model::uuid();
            {
                QSqlDatabase database=QSqlDatabase::addDatabase("QSQLITE",connection);
                database.setDatabaseName(path); database.setConnectOptions("QSQLITE_OPEN_READONLY"); QVERIFY(database.open());
                QSqlQuery tombstone(database); tombstone.prepare("SELECT 1 FROM tombstones WHERE entity_type='card' AND entity_id=?"); tombstone.addBindValue(id);
                QVERIFY(tombstone.exec()); QVERIFY(tombstone.next()); database.close();
            }
            QSqlDatabase::removeDatabase(connection);
        }
    }
    void historyMoveIntoDeletedParentGetsRepaired() {
        AppController desktop(m_temporary.filePath("parent-desktop")), phone(m_temporary.filePath("parent-phone"));
        QVERIFY(waitIdle(desktop)); QVERIFY(waitIdle(phone));
        const QString parent = create(desktop, "Parent"), child = create(desktop, "Child");
        QVERIFY(desktop.moveDeck(child, parent)); QVERIFY(waitIdle(desktop));
        QVERIFY(exchange(desktop)); QVERIFY(exchange(phone));
        QSignalSpy actions(&desktop, &AppController::actionRecorded);
        QVERIFY(desktop.moveDeck(child, {})); QVERIFY(waitIdle(desktop));
        const QString action = actions.last().first().toString();
        QVERIFY(exchange(desktop)); QVERIFY(exchange(phone));
        QVERIFY(desktop.applyHistory(action, false)); QVERIFY(waitIdle(desktop));
        phone.deleteDeck(parent); QVERIFY(waitIdle(phone)); QVERIFY(exchange(phone));
        QVERIFY(exchange(desktop)); QVERIFY(exchange(phone)); QVERIFY(exchange(desktop));
        AppController fresh(m_temporary.filePath("parent-fresh")); QVERIFY(waitIdle(fresh)); QVERIFY(exchange(fresh));
        QCOMPARE(deckParents(desktop), deckParents(fresh)); QCOMPARE(deckParents(phone), deckParents(fresh));
        QVERIFY(deckParents(fresh).contains(child)); QCOMPARE(deckParents(fresh).value(child).toString(), QString());
    }
    void restoredScopedChildUsesItsCurrentParent() {
        AppController desktop(m_temporary.filePath("restore-scope-desktop")), phone(m_temporary.filePath("restore-scope-phone"));
        QVERIFY(waitIdle(desktop)); QVERIFY(waitIdle(phone));
        const QString parent=create(desktop,"Parent"),child=create(desktop,"Child");
        QVERIFY(desktop.saveCard({},child,"basic","Question","Answer",{},1)); QVERIFY(waitIdle(desktop));
        QVERIFY(exchange(desktop)); QVERIFY(exchange(phone));
        desktop.deleteDeck(parent); QVERIFY(waitIdle(desktop));
        QVERIFY(phone.moveDeck(child,parent)); QVERIFY(waitIdle(phone)); QVERIFY(exchange(phone));
        QVERIFY(exchange(desktop));
        QSignalSpy actions(&phone,&AppController::actionRecorded);
        QVERIFY(phone.moveDeck(child,{})); QVERIFY(waitIdle(phone));
        QVERIFY(phone.applyHistory(actions.last().first().toString(),false)); QVERIFY(waitIdle(phone));
        QVERIFY(exchange(phone)); QVERIFY(exchange(desktop));
        AppController fresh(m_temporary.filePath("restore-scope-fresh")); QVERIFY(waitIdle(fresh)); QVERIFY(exchange(fresh));
        QCOMPARE(deckParents(phone),deckParents(fresh)); QCOMPARE(deckParents(desktop),deckParents(fresh));
        QVERIFY(deckParents(fresh).contains(child)); QCOMPARE(deckParents(fresh).value(child).toString(),QString());
        QVERIFY(phone.cards().isEmpty()); QVERIFY(fresh.cards().isEmpty());
    }
    void recreatedVariantRejectsReviewFromRemovedGeneration_data() {
        QTest::addColumn<QString>("kind"); QTest::addColumn<QString>("reducedKind");
        QTest::addColumn<QString>("front"); QTest::addColumn<QString>("reducedFront");
        QTest::addColumn<QString>("back"); QTest::addColumn<QString>("removedKey");
        QTest::newRow("reverse") << QString("reverse") << QString("basic") << QString("Question") << QString("Question") << QString("Answer") << QString("reverse");
        QTest::newRow("numbered-cloze") << QString("cloze") << QString("cloze") << QString("{{c1::One}} {{c2::Two}}") << QString("{{c1::One}}") << QString() << QString("c2");
    }
    void recreatedVariantRejectsReviewFromRemovedGeneration() {
        QFETCH(QString,kind); QFETCH(QString,reducedKind); QFETCH(QString,front);
        QFETCH(QString,reducedFront); QFETCH(QString,back); QFETCH(QString,removedKey);
        const QString suffix=QString::fromLatin1(QTest::currentDataTag());
        AppController desktop(m_temporary.filePath("variant-desktop-"+suffix)), phone(m_temporary.filePath("variant-phone-"+suffix));
        QVERIFY(waitIdle(desktop)); QVERIFY(waitIdle(phone));
        const QString deck = create(desktop, "Deck");
        QVERIFY(desktop.saveCard({}, deck, kind, front, back, {}, 1)); QVERIFY(waitIdle(desktop));
        const QString id = desktop.cards().first().toMap().value("id").toString();
        QVERIFY(exchange(desktop)); QVERIFY(exchange(phone));
        phone.startReview(deck); QVERIFY(waitIdle(phone)); phone.selectReviewCard(betterflash::model::variantId(id, removedKey)); QVERIFY(waitIdle(phone)); phone.revealAnswer(); QVERIFY(waitIdle(phone)); phone.grade(3); QVERIFY(waitIdle(phone));
        QVERIFY(desktop.saveCard(id, deck, reducedKind, reducedFront, back, {}, 1)); QVERIFY(waitIdle(desktop));
        const QVariantMap held = upload(prepare(desktop));
        QVERIFY(desktop.saveCard(id, deck, kind, front, back, {}, 1)); QVERIFY(waitIdle(desktop));
        QVERIFY(exchange(phone));
        QVERIFY(receive(desktop, held)); QVERIFY(exchange(desktop)); QVERIFY(exchange(phone));
        AppController fresh(m_temporary.filePath("variant-fresh-"+suffix)); QVERIFY(waitIdle(fresh)); QVERIFY(exchange(fresh));
        QCOMPARE(schedules(desktop), schedules(fresh)); QCOMPARE(schedules(phone), schedules(fresh));
    }
    void independentReviewCannotCrossLaterUndoBarrier() {
        AppController desktop(m_temporary.filePath("undo-desktop")), phone(m_temporary.filePath("undo-phone"));
        QVERIFY(waitIdle(desktop)); QVERIFY(waitIdle(phone));
        const QString deck = create(desktop, "Deck");
        QVERIFY(desktop.saveCard({}, deck, "basic", "Question", "Answer", {}, 1)); QVERIFY(waitIdle(desktop));
        desktop.startReview(deck); QVERIFY(waitIdle(desktop)); desktop.revealAnswer(); QVERIFY(waitIdle(desktop)); desktop.grade(0); QVERIFY(waitIdle(desktop));
        desktop.stopReview(); QVERIFY(waitIdle(desktop));
        QVERIFY(exchange(desktop)); QVERIFY(exchange(phone));
        for (const QString &path : {desktop.storagePath(), phone.storagePath()}) {
            const QString connection = betterflash::model::uuid();
            {
                QSqlDatabase database = QSqlDatabase::addDatabase("QSQLITE", connection);
                database.setDatabaseName(path); QVERIFY(database.open());
                QSqlQuery due(database);
                QVERIFY(due.exec("UPDATE review_variants SET due='2020-01-01T00:00:00.000Z'"));
                database.close();
            }
            QSqlDatabase::removeDatabase(connection);
        }
        QSignalSpy actions(&desktop, &AppController::actionRecorded);
        desktop.startReview(deck); QVERIFY(waitIdle(desktop)); desktop.revealAnswer(); QVERIFY(waitIdle(desktop)); desktop.grade(3); QVERIFY(waitIdle(desktop));
        const QString action = actions.last().first().toString();
        QVERIFY(desktop.applyHistory(action, false)); QVERIFY(waitIdle(desktop));
        phone.startReview(deck); QVERIFY(waitIdle(phone)); phone.revealAnswer(); QVERIFY(waitIdle(phone)); phone.grade(3); QVERIFY(waitIdle(phone));
        QVERIFY(exchange(phone)); QVERIFY(exchange(desktop)); QVERIFY(exchange(phone));
        AppController fresh(m_temporary.filePath("undo-fresh")); QVERIFY(waitIdle(fresh)); QVERIFY(exchange(fresh));
        QCOMPARE(schedules(desktop), schedules(fresh)); QCOMPARE(schedules(phone), schedules(fresh));
    }
    void committedConflictNoticeSurvivesInterruptedDownloadAndRestart() {
        const QString directory=m_temporary.filePath("notice-desktop");
        QString deck;
        {
            AppController desktop(directory), phone(m_temporary.filePath("notice-phone"));
            QVERIFY(waitIdle(desktop)); QVERIFY(waitIdle(phone));
            deck=create(desktop,"Shared deck"); QVERIFY(exchange(desktop)); QVERIFY(exchange(phone));
            phone.updateDeck(deck,"Phone edit"); QVERIFY(waitIdle(phone));
            desktop.updateDeck(deck,"Desktop edit"); QVERIFY(waitIdle(desktop));
            QVERIFY(exchange(phone));
            QVariantMap first=upload(prepare(desktop));
            QVERIFY(first.value("contentConflictCount").toLongLong()>0);
            first.insert("hasMore",true);
            QVERIFY(receive(desktop,first));
            const QVariantMap notices=prepare(desktop).value("syncNotices").toMap();
            QVERIFY(notices.value("contentConflictCount").toLongLong()>0);
        }
        AppController restarted(directory); QVERIFY(waitIdle(restarted));
        QVariantMap batch=prepare(restarted);
        const QVariantMap before=batch.value("syncNotices").toMap();
        QVERIFY(before.value("contentConflictCount").toLongLong()>0);
        QVERIFY(!before.value("records").toStringList().isEmpty());
        QVERIFY(receive(restarted,upload(batch)));
        QCOMPARE(prepare(restarted).value("syncNotices").toMap(),before);
        AppController peer(m_temporary.filePath("notice-peer")); QVERIFY(waitIdle(peer)); QVERIFY(exchange(peer));
        restarted.updateDeck(deck,"Another desktop edit"); QVERIFY(waitIdle(restarted));
        peer.updateDeck(deck,"Another phone edit"); QVERIFY(waitIdle(peer));
        QVERIFY(exchange(peer)); QVERIFY(exchange(restarted));
        const QVariantMap latest=prepare(restarted).value("syncNotices").toMap();
        QVERIFY(latest.value("revision")!=before.value("revision"));
        const int pending=outboxCount(restarted.storagePath());
        restarted.acknowledgeSyncNotices(before.value("revision").toString());
        QVERIFY(QTest::qWaitFor([&restarted] {return !restarted.busy();},5000));
        QCOMPARE(restarted.lastError().value("code").toString(),QString("SYNC_NOTICE_CHANGED"));
        QCOMPARE(prepare(restarted).value("syncNotices").toMap(),latest);
        restarted.acknowledgeSyncNotices(latest.value("revision").toString()); QVERIFY(waitIdle(restarted));
        const QVariantMap cleared=prepare(restarted).value("syncNotices").toMap();
        QCOMPARE(cleared.value("contentConflictCount").toLongLong(),qint64(0)); QVERIFY(cleared.value("records").toStringList().isEmpty());
        QCOMPARE(outboxCount(restarted.storagePath()),pending);
    }
    void offlineContentAndReviewConvergeInEitherUploadOrder_data() {
        QTest::addColumn<bool>("contentFirst");
        QTest::newRow("content-first") << true;
        QTest::newRow("review-first") << false;
    }
    void offlineContentAndReviewConvergeInEitherUploadOrder() {
            QFETCH(bool, contentFirst);
            const QString label = contentFirst ? QStringLiteral("content-first") : QStringLiteral("review-first");
            const QString sourceDirectory = m_temporary.filePath(label + QStringLiteral("-source"));
            AppController source(sourceDirectory);
            QVERIFY(waitIdle(source));
            source.createDeck(label + QStringLiteral(" deck"));
            QVERIFY(waitIdle(source));
            const QString deckId = source.decks().first().toMap().value(QStringLiteral("id")).toString();
            QVERIFY(source.saveCard({}, deckId, QStringLiteral("reverse"), QStringLiteral("Original question"),
                QStringLiteral("Original answer"), {}, 1));
            QVERIFY(waitIdle(source));
            const QString cardId = source.cards().first().toMap().value(QStringLiteral("id")).toString();
            SyncController sourceSync(&source, source.mediaPath());
            configureSync(sourceSync, m_endpoint);
            QVERIFY2(synchronize(sourceSync), qPrintable(sourceSync.error()));

            AppController desktop(m_temporary.filePath(label + QStringLiteral("-desktop")));
            AppController phone(m_temporary.filePath(label + QStringLiteral("-phone")));
            QVERIFY(waitIdle(desktop));
            QVERIFY(waitIdle(phone));
            SyncController desktopSync(&desktop, desktop.mediaPath());
            SyncController phoneSync(&phone, phone.mediaPath());
            configureSync(desktopSync, m_endpoint);
            configureSync(phoneSync, m_endpoint);
            QVERIFY2(synchronize(desktopSync), qPrintable(desktopSync.error()));
            QVERIFY2(synchronize(phoneSync), qPrintable(phoneSync.error()));
            QCOMPARE(desktop.cards().size(), 1);
            QCOMPARE(phone.cards().size(), 1);

            QVERIFY(desktop.saveCard(cardId, deckId, QStringLiteral("reverse"), QStringLiteral("Edited offline question"),
                QStringLiteral("Edited offline answer"), {}, 1));
            QVERIFY(waitIdle(desktop));
            phone.startReview(deckId);
            QVERIFY(waitIdle(phone));
            QVERIFY(phone.reviewing());
            phone.revealAnswer();
            QVERIFY(waitIdle(phone));
            phone.grade(3);
            QVERIFY(waitIdle(phone));

            if (contentFirst) {
                QVERIFY2(synchronize(desktopSync), qPrintable(desktopSync.error()));
                QVERIFY2(synchronize(phoneSync), qPrintable(phoneSync.error()));
            } else {
                QVERIFY2(synchronize(phoneSync), qPrintable(phoneSync.error()));
                QVERIFY2(synchronize(desktopSync), qPrintable(desktopSync.error()));
            }
            QVERIFY2(synchronize(desktopSync), qPrintable(desktopSync.error()));
            QVERIFY2(synchronize(phoneSync), qPrintable(phoneSync.error()));

            AppController fresh(m_temporary.filePath(label + QStringLiteral("-fresh")));
            QVERIFY(waitIdle(fresh));
            SyncController freshSync(&fresh, fresh.mediaPath());
            configureSync(freshSync, m_endpoint);
            QVERIFY2(synchronize(freshSync), qPrintable(freshSync.error()));
            QCOMPARE(fresh.cards().size(), 1);
            QCOMPARE(fresh.cards().first().toMap().value(QStringLiteral("front")).toString(), QStringLiteral("Edited offline question"));
            QCOMPARE(fresh.history().size(), 1);
            const struct DatabaseSnapshot desktopState = readSnapshot(desktop.storagePath(), cardId);
            const struct DatabaseSnapshot phoneState = readSnapshot(phone.storagePath(), cardId);
            const struct DatabaseSnapshot freshState = readSnapshot(fresh.storagePath(), cardId);
            QVERIFY(desktopState.valid);
            QVERIFY(phoneState.valid);
            QVERIFY(freshState.valid);
            QCOMPARE(desktopState.front, freshState.front);
            QCOMPARE(phoneState.front, freshState.front);
            QCOMPARE(desktopState.forward, freshState.forward);
            QCOMPARE(phoneState.forward, freshState.forward);
            QCOMPARE(desktopState.reverse, freshState.reverse);
            QCOMPARE(phoneState.reverse, freshState.reverse);
            QCOMPARE(desktopState.historyCount, 1);
            QCOMPARE(phoneState.historyCount, 1);
            QCOMPARE(freshState.historyCount, 1);

            phone.stopReview(); QVERIFY(waitIdle(phone));
            desktop.resetCard(cardId); QVERIFY(waitIdle(desktop));
            QVERIFY2(synchronize(desktopSync), qPrintable(desktopSync.error()));
            QVERIFY2(synchronize(phoneSync), qPrintable(phoneSync.error()));
            QVERIFY2(synchronize(freshSync), qPrintable(freshSync.error()));
            for (const QString &path : {desktop.storagePath(), phone.storagePath(), fresh.storagePath()}) {
                const struct DatabaseSnapshot reset = readSnapshot(path, cardId);
                QVERIFY(reset.valid);
                QCOMPARE(reset.forward.value(QStringLiteral("reviewCount")).toInt(), 0);
                QCOMPARE(reset.reverse.value(QStringLiteral("reviewCount")).toInt(), 0);
                QCOMPARE(reset.historyCount, 1);
            }
    }
    void concurrentReviewUploadsConvergeAndRetainIndependentVariants() {
        AppController source(m_temporary.filePath(QStringLiteral("concurrent-review-source")));
        QVERIFY(waitIdle(source));
        source.createDeck(QStringLiteral("Concurrent review deck"));
        QVERIFY(waitIdle(source));
        const QString deckId = source.decks().first().toMap().value(QStringLiteral("id")).toString();
        QVERIFY(source.saveCard({}, deckId, QStringLiteral("reverse"), QStringLiteral("Question"), QStringLiteral("Answer"), {}, 1));
        QVERIFY(waitIdle(source));
        const QString cardId = source.cards().first().toMap().value(QStringLiteral("id")).toString();
        SyncController sourceSync(&source, source.mediaPath());
        configureSync(sourceSync, m_endpoint);
        QVERIFY2(synchronize(sourceSync), qPrintable(sourceSync.error()));
        AppController desktop(m_temporary.filePath(QStringLiteral("concurrent-review-desktop")));
        AppController phone(m_temporary.filePath(QStringLiteral("concurrent-review-phone")));
        QVERIFY(waitIdle(desktop));
        QVERIFY(waitIdle(phone));
        SyncController desktopSync(&desktop, desktop.mediaPath());
        SyncController phoneSync(&phone, phone.mediaPath());
        configureSync(desktopSync, m_endpoint);
        configureSync(phoneSync, m_endpoint);
        QVERIFY2(synchronize(desktopSync), qPrintable(desktopSync.error()));
        QVERIFY2(synchronize(phoneSync), qPrintable(phoneSync.error()));
        QVERIFY2(synchronize(desktopSync), qPrintable(desktopSync.error()));
        QVERIFY2(synchronize(phoneSync), qPrintable(phoneSync.error()));
        desktop.startReview(deckId); QVERIFY(waitIdle(desktop));
        phone.startReview(deckId); QVERIFY(waitIdle(phone));
        QVERIFY(desktop.reviewing()); QVERIFY(phone.reviewing());
        const QString desktopVariant = desktop.currentCard().value(QStringLiteral("variantKey")).toString().isEmpty()
            ? desktop.currentCard().value(QStringLiteral("key")).toString()
            : desktop.currentCard().value(QStringLiteral("variantKey")).toString();
        const QString phoneVariant = phone.currentCard().value(QStringLiteral("variantKey")).toString().isEmpty()
            ? phone.currentCard().value(QStringLiteral("key")).toString()
            : phone.currentCard().value(QStringLiteral("variantKey")).toString();
        QVERIFY(!desktopVariant.isEmpty());
        QVERIFY(!phoneVariant.isEmpty());
        QCOMPARE(desktopVariant, phoneVariant);
        desktop.revealAnswer(); QVERIFY(waitIdle(desktop)); desktop.grade(3); QVERIFY(waitIdle(desktop));
        phone.revealAnswer(); QVERIFY(waitIdle(phone)); phone.grade(4); QVERIFY(waitIdle(phone));
        QVERIFY2(synchronize(desktopSync), qPrintable(desktopSync.error()));
        QVERIFY2(synchronize(phoneSync), qPrintable(phoneSync.error()));
        QVERIFY2(synchronize(desktopSync), qPrintable(desktopSync.error()));
        QVERIFY2(synchronize(phoneSync), qPrintable(phoneSync.error()));
        AppController fresh(m_temporary.filePath(QStringLiteral("concurrent-review-fresh")));
        QVERIFY(waitIdle(fresh));
        SyncController freshSync(&fresh, fresh.mediaPath());
        configureSync(freshSync, m_endpoint);
        QVERIFY2(synchronize(freshSync), qPrintable(freshSync.error()));
        const struct DatabaseSnapshot state = readSnapshot(fresh.storagePath(), cardId);
        QVERIFY(state.valid);
        const QVariantMap reviewed = desktopVariant == QStringLiteral("forward") ? state.forward : state.reverse;
        const QVariantMap untouched = desktopVariant == QStringLiteral("forward") ? state.reverse : state.forward;
        QVERIFY(reviewed.value(QStringLiteral("reviewCount")).toInt() >= 1);
        QVERIFY(untouched.value(QStringLiteral("reviewCount")).toInt() == 0);
        QCOMPARE(state.historyCount, 2);
    }
    void concurrentGraduationPreservesDailySchedule_data() {
        QTest::addColumn<bool>("graduationFirst");
        QTest::addColumn<int>("staleReviews");
        QTest::addColumn<bool>("resetAfterReview");
        QTest::addColumn<bool>("phonePaged");
        QTest::addColumn<bool>("neverReviewedPostponement");
        QTest::newRow("graduation-first-equal-count") << true << 1 << false << false << false;
        QTest::newRow("learning-first-equal-count") << false << 1 << false << false << false;
        QTest::newRow("graduation-first-higher-stale-count") << true << 3 << false << false << false;
        QTest::newRow("learning-first-higher-stale-count") << false << 3 << false << false << false;
        QTest::newRow("graduation-first-stale-postponement") << true << 0 << false << false << false;
        QTest::newRow("explicit-reset-after-graduation-conflict") << true << 1 << true << false << false;
        QTest::newRow("paged-phone-graduation-first") << true << 3 << false << true << false;
        QTest::newRow("paged-phone-learning-first") << false << 3 << false << true << false;
        QTest::newRow("paged-phone-explicit-reset") << true << 1 << true << true << false;
        QTest::newRow("unreviewed-postpone-after-graduation") << true << 0 << false << true << true;
        QTest::newRow("unreviewed-postpone-before-graduation") << false << 0 << false << true << true;
    }
    void resetIntentIsStrictlyValidated() {
        const QString cardId = betterflash::model::uuid();
        const struct betterflash::model::Variant fresh{betterflash::model::variantId(cardId, "forward"),
            cardId, "forward", betterflash::model::nowUtc()};
        const QVariantMap base{{"id",betterflash::model::uuid()},{"deviceId",betterflash::model::uuid()},
            {"type",QStringLiteral("variant.upsert")},{"createdAt",betterflash::model::nowUtc()}};
        const auto valid = [&](const QVariantMap &payload) {
            QVariantMap event=base; event.insert("payload",payload);
            return betterflash::protocol::validateEvent(event).has_value();
        };
        QVERIFY(valid({{"variant",betterflash::model::toMap(fresh)}}));
        QVERIFY(valid({{"variant",betterflash::model::toMap(fresh)},{"reset",false}}));
        QVERIFY(valid({{"variant",betterflash::model::toMap(fresh)},{"reset",true}}));
        QVERIFY(!valid({{"variant",betterflash::model::toMap(fresh)},{"reset",QStringLiteral("true")}}));
        QVariantMap reviewed=betterflash::model::toMap(fresh); reviewed.insert("reviewCount",1);
        QVERIFY(!valid({{"variant",reviewed},{"reset",true}}));
        QVariantMap wrongKind=base; wrongKind.insert("type",QStringLiteral("deck.upsert"));
        wrongKind.insert("payload",QVariantMap{{"reset",false}});
        QVERIFY(!betterflash::protocol::validateEvent(wrongKind));
    }
    void concurrentGraduationPreservesDailySchedule() {
        QFETCH(bool, graduationFirst); QFETCH(int, staleReviews); QFETCH(bool, resetAfterReview); QFETCH(bool, phonePaged); QFETCH(bool, neverReviewedPostponement);
        const QString tag = QString::fromLatin1(QTest::currentDataTag());
        AppController seed(m_temporary.filePath("graduate-seed-" + tag)); QVERIFY(waitIdle(seed));
        const QString deck = create(seed, "Graduation conflict"); QVERIFY(!deck.isEmpty());
        QVERIFY(seed.saveCard({}, deck, "basic", "Question", "Answer", {}, 1)); QVERIFY(waitIdle(seed));
        const QString cardId = seed.cards().first().toMap().value("id").toString();
        QVariantMap initial = prepare(seed); QVariantList events = initial.value("events").toList();
        bool seeded = false;
        for (QVariant &value : events) {
            QVariantMap event = value.toMap();
            if (event.value("type") != QStringLiteral("card.upsert")) continue;
            QVariantMap payload = event.value("payload").toMap();
            QVariantList variants = payload.value("variants").toList();
            QVERIFY(variants.size() == 1);
            QVariantMap variant = variants.first().toMap();
            variant.insert("due", QDateTime::currentDateTimeUtc().addSecs(-60).toString(Qt::ISODateWithMs));
            variant.insert("reviewCount", neverReviewedPostponement ? 0 : 3);
            variant.insert("stability", neverReviewedPostponement ? betterflash::scheduler::minimumStabilityDays : 1.0/3.0);
            variant.insert("graduated", false);
            variants[0] = variant; payload.insert("variants", variants); event.insert("payload", payload); value = event; seeded = true;
        }
        QVERIFY(seeded); initial.insert("events", events); QVERIFY(!upload(initial).contains("error"));
        AppController desktop(m_temporary.filePath("graduate-desktop-" + tag)), phone(m_temporary.filePath("graduate-phone-" + tag));
        QVERIFY(waitIdle(desktop)); QVERIFY(waitIdle(phone)); QVERIFY(exchange(desktop)); QVERIFY(exchange(phone));
        const auto makeDue = [&](AppController &app) {
            const QString connection = betterflash::model::uuid();
            bool ok = false;
            {
                QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", connection);
                db.setDatabaseName(app.storagePath());
                if (db.open()) {
                    QSqlQuery q(db); q.prepare("UPDATE review_variants SET due=? WHERE card_id=?");
                    q.addBindValue(QDateTime::currentDateTimeUtc().addSecs(-60).toString(Qt::ISODateWithMs)); q.addBindValue(cardId);
                    ok = q.exec() && q.numRowsAffected() == 1;
                    db.close();
                }
            }
            QSqlDatabase::removeDatabase(connection);
            return ok;
        };
        const int desktopReviews = neverReviewedPostponement ? 4 : 1;
        for (int index=0; index<desktopReviews; ++index) {
            if (index) { desktop.stopReview(); QVERIFY(waitIdle(desktop)); QVERIFY(makeDue(desktop)); }
            desktop.startReview(deck); QVERIFY(waitIdle(desktop)); desktop.revealAnswer(); QVERIFY(waitIdle(desktop));
            desktop.grade(3); QVERIFY(waitIdle(desktop));
        }
        if (staleReviews == 0) {
            phone.startReview(deck); QVERIFY(waitIdle(phone));
            phone.postponeDays(1); QVERIFY(waitIdle(phone));
        }
        for (int index = 0; index < staleReviews; ++index) {
            if (index) {
                phone.stopReview(); QVERIFY(waitIdle(phone));
                QVERIFY(makeDue(phone));
            }
            phone.startReview(deck); QVERIFY(waitIdle(phone)); phone.revealAnswer(); QVERIFY(waitIdle(phone));
            phone.grade(0); QVERIFY(waitIdle(phone));
        }
        if (resetAfterReview) { phone.resetCard(cardId); QVERIFY(waitIdle(phone)); }
        if (resetAfterReview || neverReviewedPostponement) {
            const QVariantList pending = prepare(phone).value("events").toList();
            bool observedIntent = false;
            for (const QVariant &value : pending) {
                const QVariantMap event = value.toMap();
                if (event.value("type") != QStringLiteral("variant.upsert")) continue;
                const QVariantMap payload = event.value("payload").toMap();
                observedIntent = payload.contains("reset") && payload.value("reset").toBool() == resetAfterReview;
            }
            QVERIFY(observedIntent);
        }
        const auto syncPhone = [&] { return phonePaged ? exchangeOneEventPages(phone) : exchange(phone); };
        if (graduationFirst) { QVERIFY(exchange(desktop)); QVERIFY(syncPhone()); }
        else { QVERIFY(syncPhone()); QVERIFY(exchange(desktop)); }
        QVERIFY(exchange(desktop)); QVERIFY(syncPhone());
        AppController fresh(m_temporary.filePath("graduate-fresh-" + tag)); QVERIFY(waitIdle(fresh)); QVERIFY(exchange(fresh));
        AppController paged(m_temporary.filePath("graduate-paged-" + tag)); QVERIFY(waitIdle(paged));
        QVERIFY(exchangeOneEventPages(paged));
        const struct DatabaseSnapshot a = readSnapshot(desktop.storagePath(), cardId), b = readSnapshot(phone.storagePath(), cardId), c = readSnapshot(fresh.storagePath(), cardId), d = readSnapshot(paged.storagePath(), cardId);
        QVERIFY(a.valid && b.valid && c.valid && d.valid);
        QCOMPARE(a.forward.value("graduated").toBool(), !resetAfterReview);
        QCOMPARE(b.forward.value("graduated").toBool(), !resetAfterReview);
        QCOMPARE(c.forward.value("graduated").toBool(), !resetAfterReview);
        QCOMPARE(d.forward.value("graduated").toBool(), !resetAfterReview);
        QCOMPARE(a.forward, b.forward); QCOMPARE(a.forward, c.forward); QCOMPARE(a.forward, d.forward);
        QCOMPARE(a.historyCount, staleReviews+desktopReviews); QCOMPARE(b.historyCount, staleReviews+desktopReviews); QCOMPARE(c.historyCount, staleReviews+desktopReviews); QCOMPARE(d.historyCount, staleReviews+desktopReviews);
    }
    void orphanGraduationDoesNotOverrideLegacyCreation() {
        AppController seed(m_temporary.filePath("orphan-seed")); QVERIFY(waitIdle(seed));
        const QString deck = create(seed, "Orphan generation"); QVERIFY(!deck.isEmpty());
        QVERIFY(seed.saveCard({}, deck, "basic", "Question", "Answer", {}, 1)); QVERIFY(waitIdle(seed));
        const QString cardId = seed.cards().first().toMap().value("id").toString();
        QVariantMap batch = prepare(seed); QVariantList events = batch.value("events").toList();
        int cardIndex = -1;
        QVariantMap orphan;
        for (int i=0; i<events.size(); ++i) {
            QVariantMap event = events[i].toMap();
            if (event.value("type") != QStringLiteral("card.upsert")) continue;
            QVariantMap payload = event.value("payload").toMap();
            payload.remove("freshVariantIds");
            payload.remove("restore");
            QVariantList variants = payload.value("variants").toList();
            QCOMPARE(variants.size(), 1);
            QVariantMap initial = variants.first().toMap();
            initial.insert("due", QDateTime::currentDateTimeUtc().addSecs(-60).toString(Qt::ISODateWithMs));
            variants[0] = initial; payload.insert("variants", variants); event.insert("payload", payload); events[i] = event;
            QVariantMap prior = initial; prior.insert("reviewCount", 1); prior.insert("stability", 1.0); prior.insert("graduated", true);
            orphan = {{"id",betterflash::model::uuid()},{"deviceId",event.value("deviceId")},
                      {"type",QStringLiteral("variant.upsert")},{"createdAt",betterflash::model::nowUtc()},
                      {"payload",QVariantMap{{"variant",prior}}}};
            cardIndex = i; break;
        }
        QVERIFY(cardIndex >= 0); events.insert(cardIndex, orphan); batch.insert("events", events);
        QVERIFY(!upload(batch).contains("error"));
        AppController author(m_temporary.filePath("orphan-author")); QVERIFY(waitIdle(author));
        QVERIFY(exchangeOneEventPages(author));
        const struct DatabaseSnapshot initial = readSnapshot(author.storagePath(), cardId);
        QVERIFY(initial.valid); QVERIFY(!initial.forward.value("graduated").toBool());
        author.startReview(deck); QVERIFY(waitIdle(author)); author.revealAnswer(); QVERIFY(waitIdle(author));
        author.grade(0); QVERIFY(waitIdle(author)); QVERIFY(exchange(author));
        AppController fresh(m_temporary.filePath("orphan-fresh")); QVERIFY(waitIdle(fresh)); QVERIFY(exchangeOneEventPages(fresh));
        const struct DatabaseSnapshot a = readSnapshot(author.storagePath(), cardId), b = readSnapshot(fresh.storagePath(), cardId);
        QVERIFY(a.valid && b.valid); QVERIFY(!a.forward.value("graduated").toBool());
        QCOMPARE(a.forward, b.forward); QCOMPARE(a.historyCount, 1); QCOMPARE(b.historyCount, 1);
        QCOMPARE(prepare(author).value("syncNotices").toMap().value("contentConflictCount").toLongLong(), qint64(0));
    }
    void subdayScheduleRoundTripsAcrossDevices() {
        AppController desktop(m_temporary.filePath(QStringLiteral("subday-sync-desktop")));
        AppController phone(m_temporary.filePath(QStringLiteral("subday-sync-phone")));
        QVERIFY(waitIdle(desktop)); QVERIFY(waitIdle(phone));
        const QString deck = create(desktop, QStringLiteral("Short interval sync"));
        QVERIFY(!deck.isEmpty());
        QVERIFY(desktop.saveCard({}, deck, QStringLiteral("basic"), QStringLiteral("Question"), QStringLiteral("Answer"), {}, 1));
        QVERIFY(waitIdle(desktop));
        QVERIFY(exchange(desktop)); QVERIFY(exchange(phone));

        phone.startReview(deck); QVERIFY(waitIdle(phone));
        phone.revealAnswer(); QVERIFY(waitIdle(phone));
        phone.grade(0); QVERIFY(waitIdle(phone));
        const QVariantMap reviewed = phone.cards().first().toMap();
        const QVariantMap schedule = reviewed.value(QStringLiteral("variants")).toList().first().toMap();
        QCOMPARE(schedule.value(QStringLiteral("stability")).toDouble(), betterflash::scheduler::minimumStabilityDays);
        QVERIFY(!schedule.value(QStringLiteral("graduated")).toBool());
        QVERIFY(exchange(phone)); QVERIFY(exchange(desktop));
        const QVariantMap desktopSchedule = desktop.cards().first().toMap().value(QStringLiteral("variants")).toList().first().toMap();
        QCOMPARE(desktopSchedule.value(QStringLiteral("stability")).toDouble(), betterflash::scheduler::minimumStabilityDays);
        QVERIFY(!desktopSchedule.value(QStringLiteral("graduated")).toBool());
        QCOMPARE(desktopSchedule.value(QStringLiteral("due")).toString(), schedule.value(QStringLiteral("due")).toString());
        const QString cardId = phone.cards().first().toMap().value(QStringLiteral("id")).toString();
        const struct DatabaseSnapshot state = readSnapshot(phone.storagePath(), cardId);
        QVERIFY(state.valid);
        QVERIFY(!state.reviewedAt.isEmpty());
        const QDateTime reviewedAt = QDateTime::fromString(state.reviewedAt, Qt::ISODateWithMs);
        const QDateTime due = QDateTime::fromString(schedule.value(QStringLiteral("due")).toString(), Qt::ISODateWithMs);
        QCOMPARE(reviewedAt.msecsTo(due), qint64(600000));
    }
    void graduatedScheduleRoundTripsThroughSync() {
        bool injectRemoteSchedule = false;
        bool serializedInitialPhase = false;
        qint64 serverSequence = 0;
        QString cardId;
        const auto serverEvent = [&](const struct HttpRequest &request) {
            QVariantMap response = emptyResponse(request);
            const QVariantMap batch = QJsonDocument::fromJson(request.body).toVariant().toMap();
            QVariantList accepted;
            QVariantList outgoing;
            for (const QVariant &eventValue : batch.value(QStringLiteral("events")).toList()) {
                const QVariantMap event = eventValue.toMap();
                accepted.append(event.value(QStringLiteral("id")));
                outgoing.append(QVariantMap{{QStringLiteral("seq"), ++serverSequence}, {QStringLiteral("event"), event}});
                if (event.value(QStringLiteral("type")) == QStringLiteral("card.upsert")) {
                    const QVariantList variants = event.value(QStringLiteral("payload")).toMap().value(QStringLiteral("variants")).toList();
                    serializedInitialPhase = !variants.isEmpty()
                        && variants.first().toMap().value(QStringLiteral("graduated")).metaType().id() == QMetaType::Bool
                        && !variants.first().toMap().value(QStringLiteral("graduated")).toBool();
                }
            }
            response.insert(QStringLiteral("acceptedIds"), accepted);
            if (injectRemoteSchedule) {
                const struct betterflash::model::Variant schedule{betterflash::model::variantId(cardId, QStringLiteral("forward")),
                    cardId, QStringLiteral("forward"), QStringLiteral("2026-11-08T01:00:00.000Z"), 4, 1.0, 0.5, true};
                const QVariantMap event{{QStringLiteral("id"), betterflash::model::uuid()},
                    {QStringLiteral("deviceId"), betterflash::model::uuid()},
                    {QStringLiteral("type"), QStringLiteral("variant.upsert")},
                    {QStringLiteral("createdAt"), QStringLiteral("2026-11-08T00:00:00.000Z")},
                    {QStringLiteral("payload"), QVariantMap{{QStringLiteral("variant"), betterflash::model::toMap(schedule)}}}};
                outgoing.append(QVariantMap{{QStringLiteral("seq"), ++serverSequence}, {QStringLiteral("event"), event}});
            }
            response.insert(QStringLiteral("events"), outgoing);
            response.insert(QStringLiteral("cursor"), serverSequence);
            return HttpResponse{json(response)};
        };
        LocalHttpFixture server(serverEvent);
        QVERIFY(server.listen());
        AppController app(m_temporary.filePath(QStringLiteral("graduated-sync")));
        QVERIFY(waitIdle(app));
        const QString deckId = create(app, QStringLiteral("Graduation sync"));
        QVERIFY(!deckId.isEmpty());
        QVERIFY(app.saveCard({}, deckId, QStringLiteral("basic"), QStringLiteral("Question"), QStringLiteral("Answer"), {}, 1));
        QVERIFY(waitIdle(app));
        cardId = app.cards().first().toMap().value(QStringLiteral("id")).toString();
        SyncController sync(&app, app.mediaPath());
        configureSync(sync, server.endpoint());
        QVERIFY2(synchronize(sync), qPrintable(sync.error()));
        QVERIFY(serializedInitialPhase);
        injectRemoteSchedule = true;
        QVERIFY2(synchronize(sync), qPrintable(sync.error() + QStringLiteral(" ") + app.lastError().value(QStringLiteral("detail")).toString()));
        const QVariantMap schedule = app.cards().first().toMap().value(QStringLiteral("variants")).toList().first().toMap();
        QVERIFY(schedule.value(QStringLiteral("graduated")).toBool());
        QCOMPARE(schedule.value(QStringLiteral("stability")).toDouble(), 1.0);
        QCOMPARE(schedule.value(QStringLiteral("reviewCount")).toInt(), 4);
    }
    void changedInfoIdentityPreventsUploads() {
        const auto acknowledge = [](const struct HttpRequest &request) {
            QVariantMap response = emptyResponse(request);
            const QVariantMap batch = QJsonDocument::fromJson(request.body).toVariant().toMap();
            QVariantList accepted;
            for (const QVariant &event : batch.value(QStringLiteral("events")).toList()) accepted.append(event.toMap().value(QStringLiteral("id")));
            response.insert(QStringLiteral("acceptedIds"), accepted);
            return HttpResponse{json(response)};
        };
        LocalHttpFixture first(acknowledge,
            QStringLiteral("11111111-1111-4111-8111-111111111111"));
        LocalHttpFixture second(acknowledge,
            QStringLiteral("22222222-2222-4222-8222-222222222222"));
        QVERIFY(first.listen());
        QVERIFY(second.listen());
        AppController app(m_temporary.filePath(QStringLiteral("changed-info-identity")));
        QVERIFY(waitIdle(app));
        app.createDeck(QStringLiteral("Pending upload"));
        QVERIFY(waitIdle(app));
        SyncController sync(&app, app.mediaPath());
        configureSync(sync, first.endpoint());
        QVERIFY2(synchronize(sync), qPrintable(sync.error()));
        configureSync(sync, second.endpoint());
        sync.synchronize();
        QVERIFY(QTest::qWaitFor([&] { return !sync.busy() && !app.busy(); }, 10000));
        QVERIFY(sync.error().startsWith(QStringLiteral("SYNC_COLLECTION_MISMATCH")));
        QCOMPARE(second.infoRequests, 1);
        QCOMPARE(second.requests.size(), 0);
    }
    void oldServerProtocolPreventsUploads() {
        const auto acknowledge = [](const struct HttpRequest &request) {
            return HttpResponse{json(emptyResponse(request))};
        };
        LocalHttpFixture server(acknowledge,
            QStringLiteral("11111111-1111-4111-8111-111111111111"), 3);
        QVERIFY(server.listen());
        AppController app(m_temporary.filePath(QStringLiteral("old-server-protocol")));
        QVERIFY(waitIdle(app));
        app.createDeck(QStringLiteral("Pending upload"));
        QVERIFY(waitIdle(app));
        SyncController sync(&app, app.mediaPath());
        configureSync(sync, server.endpoint());
        sync.synchronize();
        QVERIFY(QTest::qWaitFor([&] { return !sync.busy() && !app.busy(); }, 10000));
        QVERIFY(sync.error().startsWith(QStringLiteral("SYNC_PROTOCOL")));
        QVERIFY(sync.error().contains(QStringLiteral("GET /v1/info reports protocol 3")));
        QVERIFY(sync.error().contains(QStringLiteral("short-interval-schedules")));
        QCOMPARE(server.infoRequests, 1);
        QCOMPARE(server.requests.size(), 0);
    }
    void concurrentCardContentUsesServerOrderAndReportsConflict() {
        AppController desktop(m_temporary.filePath(QStringLiteral("content-conflict-desktop")));
        AppController phone(m_temporary.filePath(QStringLiteral("content-conflict-phone")));
        QVERIFY(waitIdle(desktop)); QVERIFY(waitIdle(phone));
        desktop.createDeck(QStringLiteral("Shared content")); QVERIFY(waitIdle(desktop));
        const QString deckId = desktop.decks().first().toMap().value(QStringLiteral("id")).toString();
        QVERIFY(desktop.saveCard({}, deckId, QStringLiteral("basic"), QStringLiteral("Original"), QStringLiteral("Answer"), {}, 1));
        QVERIFY(waitIdle(desktop));
        const QString cardId = desktop.cards().first().toMap().value(QStringLiteral("id")).toString();
        SyncController desktopSync(&desktop, desktop.mediaPath());
        SyncController phoneSync(&phone, phone.mediaPath());
        configureSync(desktopSync, m_endpoint); configureSync(phoneSync, m_endpoint);
        QVERIFY2(synchronize(desktopSync), qPrintable(desktopSync.error()));
        QVERIFY2(synchronize(phoneSync), qPrintable(phoneSync.error()));
        QVERIFY(desktop.saveCard(cardId, deckId, QStringLiteral("basic"), QStringLiteral("PC offline edit"), QStringLiteral("Answer"), {}, 1));
        QVERIFY(phone.saveCard(cardId, deckId, QStringLiteral("basic"), QStringLiteral("Phone offline edit"), QStringLiteral("Answer"), {}, 1));
        QVERIFY(waitIdle(desktop)); QVERIFY(waitIdle(phone));
        QVERIFY2(synchronize(desktopSync), qPrintable(desktopSync.error()));
        phoneSync.synchronize();
        QVERIFY(QTest::qWaitFor([&] { return !phoneSync.busy(); }, 30000));
        QVERIFY2(phoneSync.error().startsWith(QStringLiteral("SYNC_CONTENT_CONFLICT")), qPrintable(phoneSync.error()));
        desktopSync.synchronize();
        QVERIFY(QTest::qWaitFor([&] { return !desktopSync.busy(); }, 30000));
        QVERIFY(desktopSync.error().startsWith(QStringLiteral("SYNC_CONTENT_CONFLICT")));
        AppController fresh(m_temporary.filePath(QStringLiteral("content-conflict-fresh")));
        QVERIFY(waitIdle(fresh));
        SyncController freshSync(&fresh, fresh.mediaPath());
        configureSync(freshSync, m_endpoint); freshSync.synchronize();
        QVERIFY(QTest::qWaitFor([&] { return !freshSync.busy(); }, 30000));
        QVERIFY(freshSync.error().startsWith(QStringLiteral("SYNC_CONTENT_CONFLICT")));
        const struct DatabaseSnapshot state = readSnapshot(fresh.storagePath(), cardId);
        QVERIFY(state.valid);
        QCOMPARE(state.front, QStringLiteral("Phone offline edit"));
        QCOMPARE(readSnapshot(desktop.storagePath(), cardId).front, state.front);
        QCOMPARE(readSnapshot(phone.storagePath(), cardId).front, state.front);
        QCOMPARE(outboxCount(desktop.storagePath()), 0);
        QCOMPARE(outboxCount(phone.storagePath()), 0);
    }
    void concurrentDeckMovesRepairCycles_data() {
        QTest::addColumn<bool>("desktopFirst");
        QTest::newRow("desktop-first") << true;
        QTest::newRow("phone-first") << false;
    }
    void concurrentDeckMovesRepairCycles() {
        QFETCH(bool, desktopFirst);
        const QString label = desktopFirst ? QStringLiteral("desktop-first") : QStringLiteral("phone-first");
        AppController source(m_temporary.filePath(label + QStringLiteral("-move-source")));
        QVERIFY(waitIdle(source));
        source.createDeck(QStringLiteral("Deck A")); QVERIFY(waitIdle(source));
        source.createDeck(QStringLiteral("Deck B")); QVERIFY(waitIdle(source));
        const QString deckA = source.decks().at(0).toMap().value(QStringLiteral("id")).toString();
        const QString deckB = source.decks().at(1).toMap().value(QStringLiteral("id")).toString();
        QVERIFY(!deckA.isEmpty()); QVERIFY(!deckB.isEmpty());
        SyncController sourceSync(&source, source.mediaPath()); configureSync(sourceSync, m_endpoint);
        QVERIFY2(synchronize(sourceSync), qPrintable(sourceSync.error()));
        AppController desktop(m_temporary.filePath(label + QStringLiteral("-move-desktop")));
        AppController phone(m_temporary.filePath(label + QStringLiteral("-move-phone")));
        QVERIFY(waitIdle(desktop)); QVERIFY(waitIdle(phone));
        SyncController desktopSync(&desktop, desktop.mediaPath()); configureSync(desktopSync, m_endpoint);
        SyncController phoneSync(&phone, phone.mediaPath()); configureSync(phoneSync, m_endpoint);
        QVERIFY2(synchronize(desktopSync), qPrintable(desktopSync.error()));
        QVERIFY2(synchronize(phoneSync), qPrintable(phoneSync.error()));
        QVERIFY(desktop.moveDeck(deckA, deckB)); QVERIFY(waitIdle(desktop));
        QVERIFY(phone.moveDeck(deckB, deckA)); QVERIFY(waitIdle(phone));
        if (desktopFirst) {
            QVERIFY2(synchronizeAllowHierarchyResolution(desktopSync), qPrintable(desktopSync.error()));
            QVERIFY2(synchronizeAllowHierarchyResolution(phoneSync), qPrintable(phoneSync.error()));
        } else {
            QVERIFY2(synchronizeAllowHierarchyResolution(phoneSync), qPrintable(phoneSync.error()));
            QVERIFY2(synchronizeAllowHierarchyResolution(desktopSync), qPrintable(desktopSync.error()));
        }
        QVERIFY2(synchronizeAllowHierarchyResolution(desktopSync), qPrintable(desktopSync.error()));
        QVERIFY2(synchronizeAllowHierarchyResolution(phoneSync), qPrintable(phoneSync.error()));
        AppController fresh(m_temporary.filePath(label + QStringLiteral("-move-fresh")));
        QVERIFY(waitIdle(fresh));
        SyncController freshSync(&fresh, fresh.mediaPath()); configureSync(freshSync, m_endpoint);
        QVERIFY2(synchronizeAllowHierarchyResolution(freshSync), qPrintable(freshSync.error()));
        const QVariantMap expected = desktopFirst
            ? QVariantMap{{deckA, deckB}, {deckB, QString()}}
            : QVariantMap{{deckA, QString()}, {deckB, deckA}};
        QCOMPARE(deckParents(desktop), expected);
        QCOMPARE(deckParents(phone), expected);
        QCOMPARE(deckParents(fresh), expected);
        QCOMPARE(outboxCount(desktop.storagePath()), 0);
        QCOMPARE(outboxCount(phone.storagePath()), 0);
        QCOMPARE(outboxCount(fresh.storagePath()), 0);
    }
    void staleDeckMoveRepairPreservesCardsAndConverges() {
        AppController desktop(m_temporary.filePath(QStringLiteral("stale-parent-desktop")));
        AppController phone(m_temporary.filePath(QStringLiteral("stale-parent-phone")));
        QVERIFY(waitIdle(desktop)); QVERIFY(waitIdle(phone));
        desktop.createDeck(QStringLiteral("Deleted destination")); QVERIFY(waitIdle(desktop));
        const QString parent = desktop.decks().first().toMap().value(QStringLiteral("id")).toString();
        desktop.createDeck(QStringLiteral("Moved deck")); QVERIFY(waitIdle(desktop));
        QString moved;
        for (const QVariant &value : desktop.decks()) {
            const QString id = value.toMap().value(QStringLiteral("id")).toString();
            if (id != parent) moved = id;
        }
        QVERIFY(!moved.isEmpty());
        QVERIFY(desktop.saveCard({}, moved, QStringLiteral("basic"), QStringLiteral("Retained question"), QStringLiteral("Answer"), {}, 1));
        QVERIFY(waitIdle(desktop));
        const QString cardId = desktop.cards().first().toMap().value(QStringLiteral("id")).toString();
        SyncController desktopSync(&desktop, desktop.mediaPath()), phoneSync(&phone, phone.mediaPath());
        configureSync(desktopSync, m_endpoint); configureSync(phoneSync, m_endpoint);
        QVERIFY2(synchronize(desktopSync), qPrintable(desktopSync.error()));
        QVERIFY2(synchronize(phoneSync), qPrintable(phoneSync.error()));
        desktop.deleteDeck(parent); QVERIFY(waitIdle(desktop));
        QVERIFY2(synchronize(desktopSync), qPrintable(desktopSync.error()));
        QVERIFY(phone.moveDeck(moved, parent)); QVERIFY(waitIdle(phone));
        QVERIFY2(synchronizeAllowHierarchyResolution(phoneSync), qPrintable(phoneSync.error()));
        QVERIFY2(synchronizeAllowHierarchyResolution(desktopSync), qPrintable(desktopSync.error()));
        AppController fresh(m_temporary.filePath(QStringLiteral("stale-parent-fresh"))); QVERIFY(waitIdle(fresh));
        SyncController freshSync(&fresh, fresh.mediaPath()); configureSync(freshSync, m_endpoint);
        QVERIFY2(synchronizeAllowHierarchyResolution(freshSync), qPrintable(freshSync.error()));
        const QVariantMap expected{{moved, QString()}};
        for (const AppController *const app : {&desktop, &phone, &fresh}) {
            QCOMPARE(deckParents(*app), expected);
            const struct DatabaseSnapshot state = readSnapshot(app->storagePath(), cardId);
            QVERIFY(state.valid);
            QCOMPARE(state.front, QStringLiteral("Retained question"));
            QCOMPARE(outboxCount(app->storagePath()), 0);
        }
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
        configureSync(firstSync, m_endpoint); configureSync(secondSync, m_endpoint);
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
        second.revealAnswer(); QVERIFY(waitIdle(second));
        second.grade(1, 0.5); QVERIFY(waitIdle(second));
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
        configureSync(sync, server.endpoint());
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
        configureSync(sync, server.endpoint());
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
        configureSync(sync, server.endpoint());
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
        configureSync(sync, server.endpoint());
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
        configureSync(sync, server.endpoint());
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
        configureSync(sync, server.endpoint());
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
        configureSync(sync, server.endpoint());
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
        configureSync(sync, server.endpoint());
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
        qunsetenv("BETTERFLASH_SYNC_TOKEN");
    }
    void cleanup() {
        m_server.terminate();
        if (!m_server.waitForFinished(3000)) { m_server.kill(); m_server.waitForFinished(3000); }
    }
};

QTEST_MAIN(IntegrationTest)
#include "integration_test.moc"
