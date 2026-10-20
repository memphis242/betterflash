#include "SyncController.h"
#include "MediaStore.h"
#include "core/appcontroller.h"
#include "core/mediabackup.h"
#include "core/syncprotocol.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSet>
#include <QtConcurrent>
#include <cassert>
#include <cmath>
#include <optional>

namespace {
inline constexpr qsizetype maxSyncImages = 64;
inline constexpr qint64 maxResponseBytes = 8 * 1024 * 1024;
static_assert(betterflash::model::maxMediaBytes >= betterflash::model::maxImageBytes);

std::optional<qint64> responseCount(const QVariant &value, const qint64 fallback = 0) {
    if (!value.isValid()) return fallback;
    switch (value.metaType().id()) {
    case QMetaType::Int: case QMetaType::UInt: case QMetaType::LongLong:
    case QMetaType::ULongLong: case QMetaType::Double: break;
    default: return std::nullopt;
    }
    const double count = value.toDouble();
    if (!std::isfinite(count) || count < 0 || count > 1000000000 || std::floor(count) != count) return std::nullopt;
    return static_cast<qint64>(count);
}

std::expected<QStringList, QString> mediaReferences(const QVariantList &events, const bool wrapped) {
    QList<struct betterflash::model::Card> cards;
    for (const QVariant &value : events) {
        const QVariantMap event = wrapped ? value.toMap().value(QStringLiteral("event")).toMap() : value.toMap();
        const QString type = event.value(QStringLiteral("type")).toString();
        const QVariantMap payload = event.value(QStringLiteral("payload")).toMap();
        QVariantList records;
        if (type == QStringLiteral("card.upsert")) records.append(payload.value(QStringLiteral("card")));
        else if (type == QStringLiteral("history.apply")) {
            for (const QVariant &entry : payload.value(QStringLiteral("changes")).toList()) {
                const QVariantMap change = entry.toMap();
                if (change.value(QStringLiteral("entity")) == QStringLiteral("card")
                    && change.value(QStringLiteral("after")).metaType().id() == QMetaType::QVariantMap)
                    records.append(change.value(QStringLiteral("after")));
            }
        }
        for (const QVariant &record : records) {
            const auto card = betterflash::model::cardFromMap(record.toMap());
            if (!card) return std::unexpected(card.error());
            cards.append(*card);
        }
    }
    const auto names = betterflash::model::mediaReferences(cards);
    if (!names) return std::unexpected(names.error());
    if (names->size() > maxSyncImages)
        return std::unexpected(QStringLiteral("A sync page references more than 64 images. Shorten the affected card or update the server."));
    return names;
}
}
SyncController::SyncController(AppController *app, const QString &mediaDirectory, QObject *parent)
    : QObject(parent), m_app(app), m_mediaDirectory(mediaDirectory) {
    Q_ASSERT(m_app);
    m_endpoint = m_settings.value(QStringLiteral("sync/endpoint")).toString();
    m_lastSync = m_settings.value(QStringLiteral("sync/lastSync")).toString();
    configureCredentials();
    connect(m_app, &AppController::syncBatchReady, this, &SyncController::handleBatch);
    connect(m_app, &AppController::syncResponseApplied, this, &SyncController::finishApply);
    connect(m_app, &AppController::syncBatchFailedForRequest, this, [this](const QString &requestId, const QString &code, const QString &message) {
        if (active(requestId, Phase::Preparing)) fail(code, message);
    });
}
void SyncController::configureCredentials() {
    if (m_credentials) m_credentials->deleteLater();
    const QString scope = QString::fromLatin1(QCryptographicHash::hash(m_endpoint.toUtf8(), QCryptographicHash::Sha256).toHex().left(16));
    m_credentials = new SecretStore(QStringLiteral("sync-") + scope, QStringLiteral("BETTERFLASH_SYNC_TOKEN"), this);
    connect(m_credentials, &SecretStore::changed, this, &SyncController::keyChanged);
    emit keyChanged();
}
void SyncController::setEndpoint(const QString &endpoint) {
    QString value = endpoint.trimmed();
    if (!value.isEmpty()) {
        QUrl url(value);
        QString path = url.path();
        while (path.endsWith(QLatin1Char('/'))) path.chop(1);
        if (!path.endsWith(QStringLiteral("/v1/sync"))) path += QStringLiteral("/v1/sync");
        url.setPath(path);
        value = url.toString();
    }
    if (m_endpoint == value) return;
    cancel();
    m_endpoint = value;
    m_settings.setValue(QStringLiteral("sync/endpoint"), value);
    configureCredentials();
    emit configurationChanged();
}
QNetworkRequest SyncController::request(const QUrl &url) const {
    QNetworkRequest result(url);
    result.setRawHeader("Authorization", "Bearer " + m_credentials->key().toUtf8());
    result.setRawHeader("X-BetterFlash-Protocol", "2");
    if (!m_collectionId.isEmpty()) result.setRawHeader("X-BetterFlash-Collection", m_collectionId.toUtf8());
    result.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    result.setTransferTimeout(30000);
    return result;
}
QUrl SyncController::mediaUrl(const QString &name) const {
    QUrl url(m_endpoint);
    QString path = url.path();
    path.chop(4);
    url.setPath(path + QStringLiteral("media/") + name);
    return url;
}
bool SyncController::active(const QString &requestId, const Phase phase) const {
    return m_busy && m_requestId == requestId && m_phase == phase;
}
void SyncController::resetPending() {
    m_requestId.clear();
    m_phase = Phase::Idle;
    m_busy = false;
    if (m_reply) {
        QNetworkReply *const reply = m_reply;
        m_reply = nullptr;
        reply->disconnect(this);
        reply->abort();
        reply->deleteLater();
    }
    m_batch.clear();
    m_collectionId.clear();
    m_response.clear();
    m_uploads.clear();
    m_downloads.clear();
    m_downloadedBytes = 0;
}
void SyncController::fail(const QString &code, const QString &message) {
    resetPending();
    m_status = QStringLiteral("Sync interrupted. Your local changes are saved.");
    m_error = code + QStringLiteral(": ") + message;
    emit changed();
}
void SyncController::cancel() {
    const bool wasBusy = m_busy;
    resetPending();
    if (wasBusy) {
        m_status = QStringLiteral("Sync canceled. Your local changes are saved.");
        m_error.clear();
        emit changed();
    }
}
void SyncController::synchronize() {
    if (m_busy) return;
    m_error.clear();
    const QUrl url(m_endpoint);
    const bool local = url.host() == QStringLiteral("127.0.0.1") || url.host() == QStringLiteral("localhost");
    if (!url.isValid() || url.host().isEmpty() || !url.userInfo().isEmpty()
        || !(url.scheme() == QStringLiteral("https") || (local && url.scheme() == QStringLiteral("http")))) {
        fail(QStringLiteral("SYNC_ENDPOINT"), QStringLiteral("Set an HTTPS server URL in Sync settings. HTTP is allowed on localhost.")); return;
    }
    if (!hasToken()) { fail(QStringLiteral("SYNC_TOKEN"), QStringLiteral("Add your server's access token in Sync settings.")); return; }
    m_busy = true;
    m_rounds = 0;
    m_contentConflicts = 0;
    m_hierarchyResolutions = 0;
    m_conflictedRecords.clear();
    beginBatch();
}
void SyncController::beginBatch() {
    assert(m_busy);
    assert(!m_reply);
    if (++m_rounds > 10000) { fail(QStringLiteral("SYNC_LIMIT"), QStringLiteral("This sync exceeded its batch limit. Try again to continue.")); return; }
    m_requestId = betterflash::model::uuid();
    m_phase = Phase::Preparing;
    m_batch.clear();
    m_response.clear();
    m_downloadedBytes = 0;
    m_status = QStringLiteral("Preparing saved changes.");
    const QString requestId = m_requestId;
    emit changed();
    if (active(requestId, Phase::Preparing)) m_app->requestSyncBatch(requestId);
}
void SyncController::handleBatch(const QVariantMap &batch) {
    const QString requestId = batch.value(QStringLiteral("requestId")).toString();
    if (!active(requestId, Phase::Preparing)) return;
    const auto names = mediaReferences(batch.value(QStringLiteral("events")).toList(), false);
    if (!names) { fail(QStringLiteral("SYNC_IMAGE_LIMIT"), names.error()); return; }
    m_batch = batch;
    m_batchWasFull = batch.value(QStringLiteral("hasMoreLocal")).toBool();
    m_uploads = *names;
    identifyCollection();
}
void SyncController::identifyCollection() {
    assert(m_busy);
    assert(!m_reply);
    m_phase = Phase::Identifying;
    const QString requestId = m_requestId;
    QUrl url(m_endpoint);
    QString path = url.path();
    path.chop(4);
    url.setPath(path + QStringLiteral("info"));
    m_reply = m_network.get(request(url));
    QNetworkReply *const reply = m_reply;
    connect(reply, &QNetworkReply::downloadProgress, reply, [reply](qint64 received, qint64 total) {
        if (received > 16384 || total > 16384) reply->abort();
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, requestId] {
        if (!active(requestId, Phase::Identifying) || m_reply != reply) { reply->deleteLater(); return; }
        m_reply = nullptr;
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const bool success = reply->error() == QNetworkReply::NoError && status == 200;
        const QByteArray body = reply->readAll();
        reply->deleteLater();
        if (!success) {
            fail(QStringLiteral("SYNC_IDENTITY"), status == 401 || status == 403
                ? QStringLiteral("Check your sync access token.")
                : QStringLiteral("Cannot identify this sync collection. Check the connection and upgrade the server to protocol 2."));
            return;
        }
        const QJsonDocument document = QJsonDocument::fromJson(body);
        const QVariantMap info = document.object().toVariantMap();
        const QString identity = info.value(QStringLiteral("collectionId")).toString();
        if (body.size() > 16384 || !document.isObject() || !betterflash::model::validUuid(identity)
            || info.value(QStringLiteral("protocolVersion")).toInt() != 2
            || !info.value(QStringLiteral("capabilities")).toStringList().contains(QStringLiteral("independent-schedules"))
            || !info.value(QStringLiteral("capabilities")).toStringList().contains(QStringLiteral("hierarchy-repair"))) {
            fail(QStringLiteral("SYNC_PROTOCOL"), QStringLiteral("The server must support independent schedules and deck hierarchy repair. Upgrade the server and every synced client."));
            return;
        }
        const QString bound = m_batch.value(QStringLiteral("collectionId")).toString();
        if (!bound.isEmpty() && bound != identity) {
            fail(QStringLiteral("SYNC_COLLECTION_MISMATCH"), QStringLiteral("This device is bound to a different sync collection. Restore the original endpoint and account token, or use a separate local collection."));
            return;
        }
        m_collectionId = identity;
        m_batch.insert(QStringLiteral("collectionId"), identity);
        m_phase = Phase::Uploading;
        uploadNext();
    });
    m_status = QStringLiteral("Checking the sync collection.");
    emit changed();
}
void SyncController::uploadNext() {
    const QString requestId = m_requestId;
    if (!active(requestId, Phase::Uploading)) return;
    assert(!m_reply);
    if (m_uploads.isEmpty()) { postBatch(); return; }
    const QString name = m_uploads.takeFirst();
    const auto images = betterflash::model::readBackupMedia(m_mediaDirectory, {name});
    if (!images) {
        fail(QStringLiteral("SYNC_IMAGE_MISSING"), images.error() + QStringLiteral(" Restore the image from a collection backup.")); return;
    }
    const auto valid = betterflash::model::validateMediaAttachment(name, images->first().bytes);
    if (!valid) { fail(QStringLiteral("SYNC_IMAGE_FORMAT"), valid.error()); return; }
    QNetworkRequest networkRequest = request(mediaUrl(name));
    networkRequest.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/octet-stream"));
    m_reply = m_network.put(networkRequest, images->first().bytes);
    QNetworkReply *const reply = m_reply;
    connect(reply, &QNetworkReply::finished, this, [this, reply, requestId] {
        if (!active(requestId, Phase::Uploading) || m_reply != reply) { reply->deleteLater(); return; }
        m_reply = nullptr;
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const bool success = reply->error() == QNetworkReply::NoError && status >= 200 && status < 300;
        reply->deleteLater();
        if (!success) { fail(QStringLiteral("SYNC_IMAGE_UPLOAD_%1").arg(status), QStringLiteral("The server rejected an image. Check the server token and connection.")); return; }
        uploadNext();
    });
    m_status = QStringLiteral("Uploading an attached image.");
    emit changed();
}
void SyncController::postBatch() {
    const QString requestId = m_requestId;
    if (!active(requestId, Phase::Uploading)) return;
    assert(!m_reply);
    m_phase = Phase::Exchanging;
    QNetworkRequest networkRequest = request(QUrl(m_endpoint));
    networkRequest.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    QVariantMap outgoing = m_batch;
    outgoing.remove(QStringLiteral("requestId"));
    outgoing.remove(QStringLiteral("hasMoreLocal"));
    m_reply = m_network.post(networkRequest, QJsonDocument::fromVariant(outgoing).toJson(QJsonDocument::Compact));
    QNetworkReply *const reply = m_reply;
    connect(reply, &QNetworkReply::downloadProgress, reply, [reply](qint64 received, qint64 total) {
        if (received > maxResponseBytes || total > maxResponseBytes) reply->abort();
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, requestId] {
        if (!active(requestId, Phase::Exchanging) || m_reply != reply) { reply->deleteLater(); return; }
        m_reply = nullptr;
        const int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray body = reply->readAll();
        const bool success = reply->error() == QNetworkReply::NoError && httpStatus >= 200 && httpStatus < 300;
        reply->deleteLater();
        if (!success) {
            const QVariantMap serverError = QJsonDocument::fromJson(body).object().value(QStringLiteral("error")).toObject().toVariantMap();
            const QString code = serverError.value(QStringLiteral("code")).toString();
            const QString detail = serverError.value(QStringLiteral("message")).toString();
            const QString message = httpStatus == 401 || httpStatus == 403 ? QStringLiteral("Check your sync access token.")
                : (!detail.isEmpty() && detail.size() <= 1024 ? detail
                   : QStringLiteral("Check the server URL and connection, then try again."));
            fail(code.isEmpty() || code.size() > 64 ? QStringLiteral("SYNC_HTTP_%1").arg(httpStatus) : code, message); return;
        }
        if (body.size() > maxResponseBytes) {
            fail(QStringLiteral("SYNC_RESPONSE_LIMIT"), QStringLiteral("The server returned an oversized sync page. Update the server or try again.")); return;
        }
        QJsonParseError parseError;
        const QJsonDocument document = QJsonDocument::fromJson(body, &parseError);
        if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
            fail(QStringLiteral("SYNC_RESPONSE"), QStringLiteral("The server returned an invalid sync response.")); return;
        }
        QVariantMap response = document.object().toVariantMap();
        if (response.value(QStringLiteral("collectionId")).toString() != m_collectionId) {
            fail(QStringLiteral("SYNC_COLLECTION_MISMATCH"), QStringLiteral("The server collection changed during sync. Restore the original server database and retry.")); return;
        }
        QSet<QString> submitted;
        for (const QVariant &entry : m_batch.value(QStringLiteral("events")).toList())
            submitted.insert(entry.toMap().value(QStringLiteral("id")).toString());
        const auto valid = betterflash::protocol::validateResponse(response, m_batch.value(QStringLiteral("cursor")).toLongLong(), submitted);
        if (!valid) { fail(QStringLiteral("SYNC_RESPONSE"), valid.error()); return; }
        if (response.value(QStringLiteral("acceptedIds")).toList().size() != submitted.size()) {
            fail(QStringLiteral("SYNC_ACKNOWLEDGEMENT"), QStringLiteral("The server did not acknowledge every submitted change. Your pending changes are retained. Update the server and sync again.")); return;
        }
        const QVariant conflicts = response.value(QStringLiteral("contentConflicts"));
        if (conflicts.isValid() && (conflicts.metaType().id() != QMetaType::QVariantList || conflicts.toList().size() > 1000)) {
            fail(QStringLiteral("SYNC_RESPONSE"), QStringLiteral("The server returned invalid content conflict information.")); return;
        }
        const auto conflictCount = responseCount(response.value(QStringLiteral("contentConflictCount")), conflicts.toList().size());
        const auto hierarchyCount = responseCount(response.value(QStringLiteral("hierarchyResolutionCount")));
        if (!conflictCount || *conflictCount < conflicts.toList().size() || !hierarchyCount) {
            fail(QStringLiteral("SYNC_RESPONSE"), QStringLiteral("The server returned an invalid content conflict count.")); return;
        }
        m_contentConflicts += *conflictCount;
        m_hierarchyResolutions += *hierarchyCount;
        for (const QVariant &value : conflicts.toList()) {
            const QVariantMap conflict = value.toMap();
            const QString entity = conflict.value(QStringLiteral("entity")).toString();
            const QString id = conflict.value(QStringLiteral("id")).toString();
            if ((entity != QStringLiteral("card") && entity != QStringLiteral("deck")) || !betterflash::model::validUuid(id)) {
                fail(QStringLiteral("SYNC_RESPONSE"), QStringLiteral("The server returned an invalid conflicting record identifier.")); return;
            }
            const QString record = entity + QLatin1Char(' ') + id;
            if (m_conflictedRecords.size() < 10 && !m_conflictedRecords.contains(record)) m_conflictedRecords.append(record);
        }
        const auto references = mediaReferences(response.value(QStringLiteral("events")).toList(), true);
        if (!references) { fail(QStringLiteral("SYNC_IMAGE_LIMIT"), references.error()); return; }
        response.insert(QStringLiteral("requestId"), requestId);
        m_response = response;
        m_downloads.clear();
        const QFileInfo mediaRoot(m_mediaDirectory);
        if (!references->isEmpty() && (mediaRoot.isSymLink() || (mediaRoot.exists() && !mediaRoot.isDir()))) {
            fail(QStringLiteral("SYNC_IMAGE_PATH"), QStringLiteral("The collection's media path must be a directory without a symbolic link.")); return;
        }
        for (const QString &name : *references) {
            if (QFileInfo(QDir(m_mediaDirectory).filePath(name)).isSymLink()) {
                fail(QStringLiteral("SYNC_IMAGE_PATH"), QStringLiteral("An attached image path is a symbolic link. Remove the link from the media directory and sync again.")); return;
            }
            const auto images = betterflash::model::readBackupMedia(m_mediaDirectory, {name});
            if (!images || !betterflash::model::validateMediaAttachment(name, images->first().bytes)) m_downloads.append(name);
        }
        m_phase = Phase::Downloading;
        downloadNext();
    });
    m_status = QStringLiteral("Exchanging saved changes.");
    emit changed();
}
void SyncController::downloadNext() {
    const QString requestId = m_requestId;
    if (!active(requestId, Phase::Downloading)) return;
    assert(!m_reply);
    if (m_downloads.isEmpty()) {
        m_phase = Phase::Applying;
        m_status = QStringLiteral("Applying received changes.");
        const QVariantMap response = m_response;
        m_app->applySyncResponse(response);
        if (active(requestId, Phase::Applying)) emit changed();
        return;
    }
    const QString name = m_downloads.takeFirst();
    m_reply = m_network.get(request(mediaUrl(name)));
    QNetworkReply *const reply = m_reply;
    const qint64 remaining = betterflash::model::maxMediaBytes - m_downloadedBytes;
    connect(reply, &QNetworkReply::downloadProgress, reply, [reply, remaining](qint64 received, qint64 total) {
        if (received > betterflash::model::maxImageBytes || total > betterflash::model::maxImageBytes
            || received > remaining || total > remaining) reply->abort();
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, name, requestId] {
        if (!active(requestId, Phase::Downloading) || m_reply != reply) { reply->deleteLater(); return; }
        m_reply = nullptr;
        const QByteArray bytes = reply->readAll();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const bool success = reply->error() == QNetworkReply::NoError && status == 200;
        reply->deleteLater();
        if (!success || bytes.isEmpty() || bytes.size() > betterflash::model::maxImageBytes) {
            fail(QStringLiteral("SYNC_IMAGE_DOWNLOAD"), QStringLiteral("An image is missing or exceeds the 20 MiB limit. Sync the originating device, then try again.")); return;
        }
        if (bytes.size() > betterflash::model::maxMediaBytes - m_downloadedBytes) {
            fail(QStringLiteral("SYNC_IMAGE_LIMIT"), QStringLiteral("The sync page exceeds the 64 MiB image download limit. Update the server and sync again.")); return;
        }
        m_downloadedBytes += bytes.size();
        m_phase = Phase::ValidatingImage;
        auto *const watcher = new QFutureWatcher<std::expected<void, QString>>(this);
        connect(watcher, &QFutureWatcher<std::expected<void, QString>>::finished, this, [this, watcher, name, bytes, requestId] {
            const auto valid = watcher->result();
            watcher->deleteLater();
            if (!active(requestId, Phase::ValidatingImage)) return;
            if (!valid) { fail(QStringLiteral("SYNC_IMAGE_FORMAT"), valid.error()); return; }
            const auto preserved = betterflash::model::preserveDamagedMedia(m_mediaDirectory, name);
            if (!preserved) { fail(QStringLiteral("SYNC_IMAGE_STORAGE"), preserved.error()); return; }
            const auto saved = betterflash::model::writeBackupMedia(m_mediaDirectory, {{name, bytes}});
            if (!saved) { fail(QStringLiteral("SYNC_IMAGE_STORAGE"), saved.error()); return; }
            m_phase = Phase::Downloading;
            downloadNext();
        });
        watcher->setFuture(QtConcurrent::run([name, bytes] { return betterflash::model::validateMediaAttachment(name, bytes); }));
        m_status = QStringLiteral("Checking an attached image.");
        emit changed();
    });
    m_status = QStringLiteral("Downloading an attached image.");
    emit changed();
}
void SyncController::finishApply(const QString &requestId, const bool success, const bool hasMore) {
    if (!active(requestId, Phase::Applying)) return;
    if (!success) { fail(QStringLiteral("SYNC_APPLY"), QStringLiteral("Cannot apply remote changes. See the collection's storage error details.")); return; }
    if (hasMore || m_batchWasFull) { beginBatch(); return; }
    resetPending();
    m_error.clear();
    m_lastSync = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    m_settings.setValue(QStringLiteral("sync/lastSync"), m_lastSync);
    if (m_contentConflicts > 0) {
        m_status = QStringLiteral("Collection synced with content conflicts.");
        m_error = QStringLiteral("SYNC_CONTENT_CONFLICT: %1 concurrent card or deck edit(s) used server arrival order. Both versions are retained in the server event log. Review the affected content and recover an earlier version from that log if needed.").arg(m_contentConflicts);
        if (!m_conflictedRecords.isEmpty()) m_error += QStringLiteral(" Affected records (up to 10): ") + m_conflictedRecords.join(QStringLiteral(", "));
    } else {
        m_status = QStringLiteral("Collection synced.");
    }
    if (m_hierarchyResolutions > 0) {
        m_status = QStringLiteral("Collection synced with repaired deck moves.");
        const QString message = QStringLiteral("%1 deck move(s) would have created a cycle or targeted a deleted parent. The server retained a valid parent and preserved both events in its log.").arg(m_hierarchyResolutions);
        m_error = m_error.isEmpty() ? QStringLiteral("SYNC_HIERARCHY_RESOLVED: ") + message : m_error + QLatin1Char(' ') + message;
    }
    emit changed();
}
