#include "ModelDownload.h"

#include <QDir>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QNetworkReply>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QTimer>
#include <QUuid>
#include <QtConcurrent>
#include <archive.h>
#include <archive_entry.h>
#include <QSet>

namespace {
bool safeRelative(const QString &name) {
    return !name.isEmpty() && !QDir::isAbsolutePath(name) && !name.contains(QLatin1Char('\\'))
        && !name.contains(QLatin1Char(':')) && !name.split(QLatin1Char('/')).contains(QStringLiteral(".."))
        && !name.split(QLatin1Char('/')).contains(QStringLiteral("."));
}
QString stampPath(const QString &directory, const struct ModelDownloadAsset &asset) {
    return QDir(directory).filePath(asset.filename + QStringLiteral(".ready.json"));
}
struct InstallResult {
    QString error{};
};
bool assetReady(const QString &directory, const struct ModelDownloadAsset &asset);
struct InstallResult installAsset(const QString &directory, const struct ModelDownloadAsset &asset,
                                 const std::shared_ptr<std::atomic_bool> &canceled) {
    QLockFile installLock(QDir(directory).filePath(QStringLiteral(".model-install.lock")));
    installLock.setStaleLockTime(2 * 60 * 60 * 1000);
    bool locked = false;
    for (int attempt = 0; attempt < 30 && !locked; ++attempt) {
        if (canceled->load()) return {QStringLiteral("MODEL_CANCELED: Model preparation canceled.")};
        locked = installLock.tryLock(1000);
    }
    if (!locked) return {QStringLiteral("MODEL_LOCK: Another BetterFlash process is preparing this model. Close it or retry shortly.")};
    const auto lockGuard = qScopeGuard([&installLock] { installLock.unlock(); });
    if (assetReady(directory, asset)) return {};
    QFile input(QDir(directory).filePath(asset.filename));
    const QFileInfo inputBefore(input.fileName());
    if (!input.open(QIODevice::ReadOnly)) return {QStringLiteral("MODEL_READ: Cannot read downloaded model. Check storage permissions.")};
    QCryptographicHash hash(QCryptographicHash::Sha256);
    while (!input.atEnd()) {
        if (canceled->load()) return {QStringLiteral("MODEL_CANCELED: Model preparation canceled.")};
        const QByteArray part = input.read(1024 * 1024);
        if (part.isEmpty() && input.error() != QFile::NoError)
            return {QStringLiteral("MODEL_READ: Reading the downloaded model failed. Check storage and try again.")};
        hash.addData(part);
    }
    if (input.size() != asset.bytes || hash.result().toHex() != asset.sha256) {
        input.close();
        const QFileInfo afterInvalid(input.fileName());
        if (inputBefore.exists() && afterInvalid.exists() && afterInvalid.size() == inputBefore.size()
            && afterInvalid.lastModified() == inputBefore.lastModified())
            QFile::remove(input.fileName());
        return {QStringLiteral("MODEL_CHECKSUM: Downloaded model did not match its published checksum. Download it again.")};
    }
    input.close();
    QJsonArray installed;
    if (!asset.extractDirectory.isEmpty()) {
        QTemporaryDir staging(QDir(directory).filePath(QStringLiteral(".extract-XXXXXX")));
        if (!staging.isValid()) return {QStringLiteral("MODEL_STORAGE: Cannot create model staging directory. Check free space and permissions.")};
        struct archive *const archive = archive_read_new();
        if (!archive) return {QStringLiteral("MODEL_ARCHIVE: Cannot create the archive reader.")};
        const auto archiveGuard = qScopeGuard([archive] {
            archive_read_close(archive);
            archive_read_free(archive);
        });
        if (archive_read_support_filter_none(archive) != ARCHIVE_OK
            || archive_read_support_filter_gzip(archive) != ARCHIVE_OK
            || archive_read_support_filter_bzip2(archive) != ARCHIVE_OK
            || archive_read_support_format_zip(archive) != ARCHIVE_OK
            || archive_read_support_format_tar(archive) != ARCHIVE_OK)
            return {QStringLiteral("MODEL_ARCHIVE: Cannot enable archive format support.")};
        const QByteArray source = QFile::encodeName(input.fileName());
        if (archive_read_open_filename(archive, source.constData(), 1024 * 1024) != ARCHIVE_OK)
            return {QStringLiteral("MODEL_ARCHIVE: Cannot read the model archive. Download it again.")};
        QString prefix = asset.archivePrefix;
        if (!prefix.isEmpty() && !prefix.endsWith(QLatin1Char('/'))) prefix.append(QLatin1Char('/'));
        qint64 expanded = 0;
        int count = 0;
        int headerResult = ARCHIVE_OK;
        QSet<QString> paths;
        struct archive_entry *entry = nullptr;
        while ((headerResult = archive_read_next_header(archive, &entry)) == ARCHIVE_OK) {
            if (++count > 10000) return {QStringLiteral("MODEL_ARCHIVE_LIMIT: Archive contains too many entries.")};
            if (canceled->load()) return {QStringLiteral("MODEL_CANCELED: Model preparation canceled.")};
            const char *const rawName = archive_entry_pathname(entry);
            if (!rawName) return {QStringLiteral("MODEL_ARCHIVE_PATH: Archive entry has no path.")};
            const QString name = QString::fromUtf8(rawName);
            if (!safeRelative(name)) return {QStringLiteral("MODEL_ARCHIVE_PATH: Archive contains an unsafe path.")};
            if (!prefix.isEmpty() && !name.startsWith(prefix)) {
                if (archive_read_data_skip(archive) != ARCHIVE_OK)
                    return {QStringLiteral("MODEL_ARCHIVE: Cannot skip archive entry.")};
                continue;
            }
            const QString relative = prefix.isEmpty() ? name : name.mid(prefix.size());
            const auto mode = archive_entry_filetype(entry);
            if (mode == AE_IFDIR) { if (archive_read_data_skip(archive) != ARCHIVE_OK) return {QStringLiteral("MODEL_ARCHIVE: Cannot skip archive directory.")}; continue; }
            if (mode != AE_IFREG || archive_entry_symlink(entry) || archive_entry_hardlink(entry))
                return {QStringLiteral("MODEL_ARCHIVE_PATH: Archive contains a symbolic or hard link.")};
            if (!safeRelative(relative) || paths.contains(relative))
                return {QStringLiteral("MODEL_ARCHIVE_PATH: Archive contains an invalid or duplicate model path.")};
            const la_int64_t declared = archive_entry_size(entry);
            if (declared < 0 || declared > 1024LL * 1024 * 1024 || expanded + declared > 2LL * 1024 * 1024 * 1024)
                return {QStringLiteral("MODEL_ARCHIVE_LIMIT: Extracted model exceeds the storage limit.")};
            expanded += declared;
            const QString outputPath = QDir(staging.path()).filePath(relative);
            if (!QDir().mkpath(QFileInfo(outputPath).path())) return {QStringLiteral("MODEL_STORAGE: Cannot create model folders.")};
            QSaveFile output(outputPath);
            if (!output.open(QIODevice::WriteOnly)) return {QStringLiteral("MODEL_STORAGE: Cannot create a model file.")};
            qint64 written = 0;
            while (true) {
                const void *data = nullptr; size_t size = 0; la_int64_t offset = 0;
                const int result = archive_read_data_block(archive, &data, &size, &offset);
                if (result == ARCHIVE_EOF) break;
                if (result != ARCHIVE_OK || canceled->load() || offset != written || size > 1024ULL * 1024 * 1024
                    || written > 1024LL * 1024 * 1024 - static_cast<qint64>(size)
                    || output.write(static_cast<const char *>(data), static_cast<qint64>(size)) != static_cast<qint64>(size))
                    return {canceled->load() ? QStringLiteral("MODEL_CANCELED: Model preparation canceled.") : QStringLiteral("MODEL_EXTRACT: Cannot extract model data. Check free space and try again.")};
                written += static_cast<qint64>(size);
            }
            if (written != declared || !output.commit()) return {QStringLiteral("MODEL_EXTRACT: Cannot extract model data. Check free space and try again.")};
            paths.insert(relative);
            installed.append(QJsonObject{{QStringLiteral("path"), relative},
                {QStringLiteral("size"), static_cast<double>(written)}});
        }
        if (headerResult != ARCHIVE_EOF)
            return {QStringLiteral("MODEL_ARCHIVE: The model archive is truncated or unreadable.")};
        if (installed.isEmpty()) return {QStringLiteral("MODEL_ARCHIVE_LAYOUT: Archive lacks the expected model files.")};
        if (canceled->load()) return {QStringLiteral("MODEL_CANCELED: Model preparation canceled.")};
        const QString destination = QDir(directory).filePath(asset.extractDirectory);
        if (QFileInfo(destination).isSymLink()) return {QStringLiteral("MODEL_STORAGE_LINK: Model folder must not be a symbolic link.")};
        if (!QDir().mkpath(QFileInfo(destination).path())) return {QStringLiteral("MODEL_STORAGE: Cannot create the model destination.")};
        const QString retired = destination + QStringLiteral(".retired-") + QUuid::createUuid().toString(QUuid::Id128);
        const bool hadDirectory = QFileInfo::exists(destination);
        if (canceled->load()) return {QStringLiteral("MODEL_CANCELED: Model preparation canceled.")};
        if (hadDirectory && !QDir().rename(destination, retired)) return {QStringLiteral("MODEL_STORAGE: Cannot replace the model folder. Close any other model preparation and try again.")};
        if (canceled->load()) {
            if (hadDirectory) QDir().rename(retired, destination);
            return {QStringLiteral("MODEL_CANCELED: Model preparation canceled.")};
        }
        if (!QDir().rename(staging.path(), destination)) {
            if (hadDirectory) QDir().rename(retired, destination);
            return {QStringLiteral("MODEL_INSTALL: Cannot install the extracted model. Check storage permissions.")};
        }
        staging.setAutoRemove(false);
        if (hadDirectory) QDir(retired).removeRecursively();
    }
    if (canceled->load()) return {QStringLiteral("MODEL_CANCELED: Model preparation canceled.")};
    const QFileInfo file(input.fileName());
    QSaveFile marker(stampPath(directory, asset));
    const QJsonObject stamp{{QStringLiteral("sha256"), QString::fromLatin1(asset.sha256)},
        {QStringLiteral("modified"), file.lastModified().toMSecsSinceEpoch()},
        {QStringLiteral("installed"), installed}};
    if (!marker.open(QIODevice::WriteOnly) || marker.write(QJsonDocument(stamp).toJson(QJsonDocument::Compact)) < 0 || !marker.commit())
        return {QStringLiteral("MODEL_STORAGE: Cannot record the verified model. Check storage permissions.")};
    return {};
}
bool assetReady(const QString &directory, const struct ModelDownloadAsset &asset) {
    const QFileInfo file(QDir(directory).filePath(asset.filename));
    if (!file.isFile() || file.isSymLink() || file.size() != asset.bytes) return false;
    QFile stamp(stampPath(directory, asset));
    if (!stamp.open(QIODevice::ReadOnly) || stamp.size() > 4 * 1024 * 1024) return false;
    const QJsonObject object = QJsonDocument::fromJson(stamp.readAll()).object();
    if (object.value(QStringLiteral("sha256")).toString().toLatin1() != asset.sha256
        || object.value(QStringLiteral("modified")).toInteger() != file.lastModified().toMSecsSinceEpoch()) return false;
    if (asset.extractDirectory.isEmpty()) return true;
    const QString root = QDir(directory).filePath(asset.extractDirectory);
    const QJsonArray files = object.value(QStringLiteral("installed")).toArray();
    if (files.isEmpty() || QFileInfo(root).isSymLink()) return false;
    for (const QJsonValue &entry : files) {
        const QJsonObject installed = entry.toObject();
        const QString relative = installed.value(QStringLiteral("path")).toString();
        if (!safeRelative(relative)) return false;
        const QFileInfo item(QDir(root).filePath(relative));
        if (!item.isFile() || item.isSymLink() || item.size() != installed.value(QStringLiteral("size")).toInteger()) return false;
    }
    return true;
}
}

ModelDownload::ModelDownload(QString directory, QObject *parent)
    : QObject(parent), m_directory(std::move(directory)) {}
ModelDownload::~ModelDownload() { cancel(); }
void ModelDownload::configure(const QList<struct ModelDownloadAsset> &assets) {
    cancel();
    m_assets = assets;
    for (const struct ModelDownloadAsset &asset : m_assets) {
        Q_ASSERT(safeRelative(asset.filename) && asset.bytes > 0 && asset.sha256.size() == 64);
        Q_ASSERT(asset.url.scheme() == QStringLiteral("https"));
        Q_ASSERT(asset.extractDirectory.isEmpty() || safeRelative(asset.extractDirectory));
    }
    m_status = ready() ? QStringLiteral("Ready for offline use") : QStringLiteral("Downloads on first use");
    m_error.clear(); m_progress = 0;
    emit changed();
}
bool ModelDownload::ready() const {
    if (m_assets.isEmpty()) return false;
    for (const struct ModelDownloadAsset &asset : m_assets) if (!assetReady(m_directory, asset)) return false;
    return true;
}
void ModelDownload::start() {
    if (m_downloading) return;
    if (ready()) { m_status = QStringLiteral("Ready for offline use"); emit changed(); emit finished(true); return; }
    if (m_assets.isEmpty()) { fail(QStringLiteral("MODEL_PLATFORM: No local model package is available for this platform.")); return; }
    if (!QDir().mkpath(m_directory) || QFileInfo(m_directory).isSymLink()) { fail(QStringLiteral("MODEL_STORAGE: Choose a writable model directory without symbolic links.")); return; }
    ++m_generation;
    m_cancel = std::make_shared<std::atomic_bool>(false);
    m_downloading = true; m_error.clear(); m_progress = 0; m_index = 0; m_completedBytes = 0; m_totalBytes = 0;
    for (const struct ModelDownloadAsset &asset : m_assets) m_totalBytes += asset.bytes;
    next();
}
void ModelDownload::next() {
    if (!m_downloading) return;
    if (m_index >= m_assets.size()) {
        m_downloading = false; m_progress = 1; m_status = QStringLiteral("Ready for offline use");
        emit changed(); emit finished(true); return;
    }
    const struct ModelDownloadAsset asset = m_assets[m_index];
    if (assetReady(m_directory, asset)) { m_completedBytes += asset.bytes; ++m_index; next(); return; }
    const QString destination = QDir(m_directory).filePath(asset.filename);
    const QFileInfo file(destination);
    if (file.isSymLink()) { fail(QStringLiteral("MODEL_STORAGE_LINK: Model files must not be symbolic links.")); return; }
    if (file.isFile() && file.size() == asset.bytes) { verifyAndInstall(); return; }
    if (!QDir().mkpath(file.path())) { fail(QStringLiteral("MODEL_STORAGE: Cannot create the model folder.")); return; }
    m_file = std::make_unique<QSaveFile>(destination);
    if (!m_file->open(QIODevice::WriteOnly)) { fail(QStringLiteral("MODEL_STORAGE: Cannot write the model download. Check free space and permissions.")); return; }
    m_hash = std::make_unique<QCryptographicHash>(QCryptographicHash::Sha256);
    m_received = 0;
    m_status = QStringLiteral("Downloading %1").arg(asset.filename); emit changed();
    QNetworkRequest request(asset.url);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setTransferTimeout(60000);
    m_reply = m_network.get(request);
    QNetworkReply *const reply = m_reply;
    reply->setReadBufferSize(256 * 1024);
    QTimer::singleShot(30 * 60 * 1000, reply, [reply] { if (!reply->isFinished()) reply->abort(); });
    connect(reply, &QIODevice::readyRead, this, [this, reply, asset] {
        if (m_reply != reply || !m_downloading) return;
        while (reply->bytesAvailable()) {
            const QByteArray chunk = reply->read(64 * 1024);
            m_received += chunk.size();
            if (m_received > asset.bytes || m_file->write(chunk) != chunk.size()) {
                fail(QStringLiteral("MODEL_DOWNLOAD_LIMIT: Model data exceeded its limit or could not be written. Check free space and retry.")); return;
            }
            m_hash->addData(chunk);
        }
        updateProgress(m_received);
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, asset] {
        if (m_reply != reply) { reply->deleteLater(); return; }
        m_reply = nullptr;
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const bool okay = reply->error() == QNetworkReply::NoError && status == 200;
        reply->deleteLater();
        if (!okay) { fail(QStringLiteral("MODEL_HTTP_%1: Model download failed. Check the connection and try again.").arg(status)); return; }
        if (m_received != asset.bytes || m_hash->result().toHex() != asset.sha256) {
            fail(QStringLiteral("MODEL_CHECKSUM: Downloaded model did not match its published checksum. Try downloading again.")); return;
        }
        if (!m_file->commit()) { fail(QStringLiteral("MODEL_STORAGE: Cannot save the downloaded model. Check free space and permissions.")); return; }
        m_file.reset(); m_hash.reset(); verifyAndInstall();
    });
}
void ModelDownload::updateProgress(qint64 current) {
    m_progress = m_totalBytes > 0 ? static_cast<double>(m_completedBytes + current) / static_cast<double>(m_totalBytes) : 0;
    emit changed();
}
void ModelDownload::verifyAndInstall() {
    Q_ASSERT(m_index < m_assets.size());
    const struct ModelDownloadAsset asset = m_assets[m_index];
    const quint64 generation = m_generation;
    m_status = asset.extractDirectory.isEmpty() ? QStringLiteral("Verifying %1").arg(asset.filename) : QStringLiteral("Preparing %1").arg(asset.filename);
    emit changed();
    auto *const watcher = new QFutureWatcher<struct InstallResult>(this);
    connect(watcher, &QFutureWatcher<struct InstallResult>::finished, this, [this, watcher, generation, asset] {
        const struct InstallResult result = watcher->result(); watcher->deleteLater();
        if (generation != m_generation || !m_downloading) return;
        if (!result.error.isEmpty()) { fail(result.error); return; }
        m_completedBytes += asset.bytes; ++m_index; updateProgress(0); next();
    });
    watcher->setFuture(QtConcurrent::run(installAsset, m_directory, asset, m_cancel));
}
void ModelDownload::fail(const QString &message) {
    ++m_generation; m_cancel->store(true);
    if (m_reply) {
        m_reply->disconnect(this); m_reply->abort(); m_reply->deleteLater(); m_reply = nullptr;
    }
    m_file.reset(); m_hash.reset(); m_downloading = false;
    m_error = message; m_status = QStringLiteral("Model preparation failed");
    emit changed(); emit finished(false);
}
void ModelDownload::cancel() {
    ++m_generation; m_cancel->store(true);
    if (m_reply) {
        m_reply->disconnect(this); m_reply->abort(); m_reply->deleteLater(); m_reply = nullptr;
    }
    m_file.reset(); m_hash.reset();
    if (m_downloading) { m_downloading = false; m_status = QStringLiteral("Download canceled"); emit changed(); emit finished(false); }
}
