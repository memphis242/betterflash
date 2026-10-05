#include "SyncController.h"
#include "MediaStore.h"
#include "core/appcontroller.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QSaveFile>

namespace {
QStringList mediaReferences(const QVariant &value) {
    const QString json = QString::fromUtf8(QJsonDocument::fromVariant(value).toJson(QJsonDocument::Compact));
    static const QRegularExpression pattern(QStringLiteral("media:([a-f0-9]{64}\\.(?:png|jpg|jpeg|webp|gif))"));
    QStringList names;
    auto matches = pattern.globalMatch(json);
    while (matches.hasNext()) names.append(matches.next().captured(1));
    names.removeDuplicates();
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
    connect(m_app, &AppController::syncApplied, this, &SyncController::finishApply);
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
void SyncController::fail(const QString &code, const QString &message) {
    m_busy = false;
    m_status = QStringLiteral("Sync interrupted. Your local changes are saved.");
    m_error = code + QStringLiteral(": ") + message;
    emit changed();
}
void SyncController::cancel() {
    if (m_reply) {
        m_reply->disconnect(this);
        m_reply->abort();
        m_reply->deleteLater();
        m_reply = nullptr;
    }
    if (m_busy) {
        m_busy = false;
        m_status = QStringLiteral("Sync canceled. Your local changes are saved.");
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
    m_status = QStringLiteral("Preparing saved changes.");
    emit changed();
    m_app->requestSyncBatch();
}
void SyncController::handleBatch(const QVariantMap &batch) {
    if (!m_busy) return;
    if (++m_rounds > 10000) { fail(QStringLiteral("SYNC_LIMIT"), QStringLiteral("This sync exceeded its batch limit. Try again to continue.")); return; }
    m_batch = batch;
    m_batchWasFull = batch.value(QStringLiteral("events")).toList().size() >= 100;
    m_uploads = mediaReferences(batch.value(QStringLiteral("events")));
    uploadNext();
}
void SyncController::uploadNext() {
    if (!m_busy) return;
    if (m_uploads.isEmpty()) { postBatch(); return; }
    const QString name = m_uploads.takeFirst();
    const QString path = QDir(m_mediaDirectory).filePath(name);
    auto *const file = new QFile(path);
    if (!MediaStore::validName(name) || QFileInfo(path).isSymLink() || !file->open(QIODevice::ReadOnly)
        || file->size() > 20 * 1024 * 1024) {
        delete file;
        fail(QStringLiteral("SYNC_IMAGE_MISSING"), QStringLiteral("A referenced image is missing. Restore the image from a collection export.")); return;
    }
    QNetworkRequest networkRequest = request(mediaUrl(name));
    networkRequest.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/octet-stream"));
    m_status = QStringLiteral("Uploading an attached image.");
    emit changed();
    m_reply = m_network.put(networkRequest, file);
    QNetworkReply *const reply = m_reply;
    file->setParent(reply);
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        m_reply = nullptr;
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const bool success = reply->error() == QNetworkReply::NoError && status >= 200 && status < 300;
        reply->deleteLater();
        if (!success) { fail(QStringLiteral("SYNC_IMAGE_UPLOAD_%1").arg(status), QStringLiteral("The server rejected an image. Check the server token and connection.")); return; }
        uploadNext();
    });
}
void SyncController::postBatch() {
    if (!m_busy) return;
    QNetworkRequest networkRequest = request(QUrl(m_endpoint));
    networkRequest.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    m_status = QStringLiteral("Exchanging saved changes.");
    emit changed();
    m_reply = m_network.post(networkRequest, QJsonDocument::fromVariant(m_batch).toJson(QJsonDocument::Compact));
    QNetworkReply *const reply = m_reply;
    connect(reply, &QNetworkReply::downloadProgress, reply, [reply](qint64 received, qint64 total) {
        if (received > 8 * 1024 * 1024 || total > 8 * 1024 * 1024) reply->abort();
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        m_reply = nullptr;
        const int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray body = reply->readAll();
        const bool success = reply->error() == QNetworkReply::NoError && httpStatus >= 200 && httpStatus < 300;
        reply->deleteLater();
        if (!success) {
            const QString message = httpStatus == 401 || httpStatus == 403 ? QStringLiteral("Check your sync access token.")
                : QStringLiteral("Check the server URL and connection, then try again.");
            fail(QStringLiteral("SYNC_HTTP_%1").arg(httpStatus), message); return;
        }
        QJsonParseError parseError;
        const QJsonDocument document = QJsonDocument::fromJson(body, &parseError);
        if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
            fail(QStringLiteral("SYNC_RESPONSE"), QStringLiteral("The server returned an invalid sync response.")); return;
        }
        m_response = document.object().toVariantMap();
        m_downloads.clear();
        const QStringList references = mediaReferences(m_response.value(QStringLiteral("events")));
        for (const QString &name : references) {
            if (!QFile::exists(QDir(m_mediaDirectory).filePath(name))) m_downloads.append(name);
        }
        downloadNext();
    });
}
void SyncController::downloadNext() {
    if (!m_busy) return;
    if (m_downloads.isEmpty()) { m_app->applySyncResponse(m_response); return; }
    const QString name = m_downloads.takeFirst();
    m_status = QStringLiteral("Downloading an attached image.");
    emit changed();
    m_reply = m_network.get(request(mediaUrl(name)));
    QNetworkReply *const reply = m_reply;
    connect(reply, &QNetworkReply::downloadProgress, reply, [reply](qint64 received, qint64 total) {
        if (received > 20 * 1024 * 1024 || total > 20 * 1024 * 1024) reply->abort();
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, name] {
        m_reply = nullptr;
        const QByteArray bytes = reply->readAll();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const bool success = reply->error() == QNetworkReply::NoError && status == 200;
        reply->deleteLater();
        if (!success || QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex()) != name.section(QLatin1Char('.'), 0, 0)) {
            fail(QStringLiteral("SYNC_IMAGE_DOWNLOAD"), QStringLiteral("An image is missing or failed its integrity check. Sync the originating device, then try again.")); return;
        }
        if (!QDir().mkpath(m_mediaDirectory)) { fail(QStringLiteral("SYNC_IMAGE_STORAGE"), QStringLiteral("Cannot create the image directory. Check its permissions.")); return; }
        QSaveFile output(QDir(m_mediaDirectory).filePath(name));
        if (!output.open(QIODevice::WriteOnly) || output.write(bytes) != bytes.size() || !output.commit()) {
            fail(QStringLiteral("SYNC_IMAGE_STORAGE"), QStringLiteral("Cannot save an image. Check free space and directory permissions.")); return;
        }
        downloadNext();
    });
}
void SyncController::finishApply(const bool success, const bool hasMore) {
    if (!m_busy) return;
    if (!success) { fail(QStringLiteral("SYNC_APPLY"), QStringLiteral("Cannot apply remote changes. See the collection's storage error details.")); return; }
    if (hasMore || m_batchWasFull) { m_app->requestSyncBatch(); return; }
    m_busy = false;
    m_error.clear();
    m_lastSync = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    m_settings.setValue(QStringLiteral("sync/lastSync"), m_lastSync);
    m_status = QStringLiteral("Collection synced.");
    emit changed();
}
