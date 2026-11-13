#include "ModelResponseCache.h"

#include <QDateTime>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QLockFile>
#include <QRegularExpression>
#include <QSaveFile>
#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstring>

#ifdef Q_OS_UNIX
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace {
constexpr qint64 MaxEntryBytes = 1024 * 1024;
constexpr qint64 MaxTotalBytes = 128LL * 1024 * 1024;
constexpr qsizetype MaxEntries = 512;
constexpr qint64 ExpiryMs = 30LL * 24 * 60 * 60 * 1000;
static_assert(MaxEntryBytes > 0 && MaxTotalBytes >= MaxEntryBytes && MaxEntries > 0 && ExpiryMs > 0);

QVariantMap error(const QString &code, const QString &message, const QString &detail) {
    return {{QStringLiteral("code"), code}, {QStringLiteral("message"), message}, {QStringLiteral("detail"), detail}};
}
#ifdef Q_OS_UNIX
QByteArray identityFromStat(const struct stat &attributes) {
    if (!S_ISREG(attributes.st_mode)) return {};
#ifdef Q_OS_MACOS
    const auto modified = attributes.st_mtimespec;
    const auto changed = attributes.st_ctimespec;
#else
    const auto modified = attributes.st_mtim;
    const auto changed = attributes.st_ctim;
#endif
    return QByteArray::number(static_cast<qulonglong>(attributes.st_dev)) + ':'
        + QByteArray::number(static_cast<qulonglong>(attributes.st_ino)) + ':'
        + QByteArray::number(static_cast<qlonglong>(attributes.st_size)) + ':'
        + QByteArray::number(static_cast<qlonglong>(modified.tv_sec)) + ':' + QByteArray::number(static_cast<qlonglong>(modified.tv_nsec)) + ':'
        + QByteArray::number(static_cast<qlonglong>(changed.tv_sec)) + ':' + QByteArray::number(static_cast<qlonglong>(changed.tv_nsec));
}
#endif
QVariantMap directoryFailure(const QString &path, const QString &why) {
    return error(QStringLiteral("LLM_CACHE_DIRECTORY"),
                 QStringLiteral("The model response cache is unavailable. Check the cache directory and its permissions; the model will still run."),
                 path + QStringLiteral(": ") + why);
}
bool ensureDirectory(const QString &path, QString *why) {
    Q_ASSERT(why);
#ifdef Q_OS_UNIX
    const QByteArray native = QFile::encodeName(path);
    if (::mkdir(native.constData(), S_IRWXU) != 0 && errno != EEXIST) {
        *why = QString::fromLocal8Bit(std::strerror(errno)); return false;
    }
    const int fd = ::open(native.constData(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (fd < 0) { *why = QString::fromLocal8Bit(std::strerror(errno)); return false; }
    struct stat info{};
    bool okay = ::fstat(fd, &info) == 0;
    if (!okay) *why = QString::fromLocal8Bit(std::strerror(errno));
    if (okay && info.st_uid != ::geteuid()) { *why = QStringLiteral("The directory is not owned by the current user."); okay = false; }
    if (okay && ::fchmod(fd, S_IRWXU) != 0) { *why = QString::fromLocal8Bit(std::strerror(errno)); okay = false; }
    if (::close(fd) != 0 && okay) { *why = QString::fromLocal8Bit(std::strerror(errno)); okay = false; }
    return okay;
#else
    const QFileInfo info(path);
    if (info.isSymLink() || (info.exists() && !info.isDir())) { *why = QStringLiteral("The path is not a safe directory."); return false; }
    if (!QDir().mkpath(path) || !QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner)) {
        *why = QStringLiteral("The directory could not be created or secured."); return false;
    }
    return true;
#endif
}
QVariantMap prepare(const QString &parent, const QString &directory) {
    QString why;
    if (!ensureDirectory(parent, &why)) return directoryFailure(parent, why);
    if (!ensureDirectory(directory, &why)) return directoryFailure(directory, why);
    return {};
}
QString pathFor(const QString &directory, const QByteArray &key) {
    Q_ASSERT(key.size() == 64);
    return QDir(directory).filePath(QString::fromLatin1(key) + QStringLiteral(".json"));
}
QVariantMap lockError(const QString &directory, const QLockFile &lock) {
    return error(QStringLiteral("LLM_CACHE_LOCK"), QStringLiteral("The model response cache could not be locked. The model will still run; check the cache directory."),
                 QDir(directory).filePath(QStringLiteral(".lock")) + QStringLiteral(": lock error %1").arg(static_cast<int>(lock.error())));
}
QVariantMap removeFile(const QFileInfo &entry) {
    QFile file(entry.absoluteFilePath());
    if (file.remove()) return {};
    return error(QStringLiteral("LLM_CACHE_REMOVE"), QStringLiteral("An expired or damaged cached response could not be removed. Check cache permissions."),
                 entry.absoluteFilePath() + QStringLiteral(": ") + file.errorString());
}
bool validMessage(const QJsonObject &message) {
    const QJsonValue role = message.value(QStringLiteral("role"));
    const QJsonValue content = message.value(QStringLiteral("content"));
    const QJsonValue calls = message.value(QStringLiteral("tool_calls"));
    if (!role.isString() || role.toString() != QStringLiteral("assistant")) return false;
    if (!content.isUndefined() && !content.isNull() && !content.isString()) return false;
    if (!calls.isUndefined() && !calls.isNull() && !calls.isArray()) return false;
    if ((!content.isString() || content.toString().trimmed().isEmpty()) && (!calls.isArray() || calls.toArray().isEmpty())) return false;
    if (content.isString() && content.toString().size() > 110000) return false;
    const QJsonValue native = message.value(QStringLiteral("_anthropic_content"));
    if (!native.isUndefined() && !native.isArray()) return false;
    return true;
}
QVariantMap pruneExpired(const QString &directory) {
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const QRegularExpression namePattern(QStringLiteral("^[0-9a-f]{64}\\.json$"));
    const QDir dir(directory);
    for (const QFileInfo &entry : dir.entryInfoList(QDir::Files | QDir::NoSymLinks | QDir::Hidden, QDir::NoSort)) {
        if (!namePattern.match(entry.fileName()).hasMatch()) continue;
        const QDateTime changed = entry.lastModified();
        if (!changed.isValid()) continue;
        const qint64 at = changed.toMSecsSinceEpoch();
        if (at <= now && now - at >= ExpiryMs) {
            if (const QVariantMap failure = removeFile(entry); !failure.isEmpty()) return failure;
        }
    }
    return {};
}
QVariantMap prune(const QString &directory, const QString &reservedName, qint64 incomingBytes) {
    QDir dir(directory);
    const QRegularExpression namePattern(QStringLiteral("^[0-9a-f]{64}\\.json$"));
    QList<QFileInfo> entries;
    qint64 total = 0;
    for (const QFileInfo &info : dir.entryInfoList(QDir::Files | QDir::NoSymLinks | QDir::Hidden, QDir::NoSort)) {
        if (!namePattern.match(info.fileName()).hasMatch() || info.fileName() == reservedName) continue;
        entries.append(info);
        total += info.size();
    }
    std::sort(entries.begin(), entries.end(), [](const QFileInfo &left, const QFileInfo &right) {
        return left.lastModified() == right.lastModified() ? left.fileName() < right.fileName() : left.lastModified() < right.lastModified();
    });
    while (!entries.isEmpty() && (entries.size() + 1 > MaxEntries || total + incomingBytes > MaxTotalBytes)) {
        const QFileInfo oldest = entries.takeFirst();
        if (const QVariantMap failure = removeFile(oldest); !failure.isEmpty()) return failure;
        total -= oldest.size();
    }
    return {};
}
}

ModelResponseCache::ModelResponseCache(const QString &dataDirectory)
    : m_parentDirectory(QDir(dataDirectory).filePath(QStringLiteral("cache"))),
      m_directory(QDir(m_parentDirectory).filePath(QStringLiteral("model-responses"))) {
    Q_ASSERT(!dataDirectory.isEmpty());
}

QByteArray ModelResponseCache::assetIdentity(const QString &path) {
#ifdef Q_OS_UNIX
    const QByteArray native = QFile::encodeName(path);
    struct stat attributes{};
    if (::lstat(native.constData(), &attributes) != 0) return {};
    return identityFromStat(attributes);
#else
    const QFileInfo info(path);
    if (!info.isFile() || info.isSymLink() || !info.lastModified().isValid() || !info.metadataChangeTime().isValid()) return {};
    return info.canonicalFilePath().toUtf8() + ':' + QByteArray::number(info.size()) + ':'
        + QByteArray::number(info.lastModified().toMSecsSinceEpoch()) + ':' + QByteArray::number(info.metadataChangeTime().toMSecsSinceEpoch());
#endif
}
struct ModelResponseCache::AssetFingerprint ModelResponseCache::fingerprintAsset(const QString &path, const std::shared_ptr<std::atomic_bool> &canceled) {
    Q_ASSERT(canceled);
    const QByteArray before = assetIdentity(path);
    if (before.isEmpty()) return {{}, {}, error(QStringLiteral("LLM_CACHE_MODEL_IDENTITY"),
        QStringLiteral("The local model could not be identified for response caching. The model can still run."), path)};
    const auto known = m_verifiedAssets.constFind(before);
    if (known != m_verifiedAssets.cend()) return {before, *known, {}};
    QFile file(path);
#ifdef Q_OS_UNIX
    const QByteArray native = QFile::encodeName(path);
    const int fd = ::open(native.constData(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
    if (fd < 0) return {{}, {}, error(QStringLiteral("LLM_CACHE_MODEL_READ"),
        QStringLiteral("The local model could not be read for response caching. The model can still run."), path + QStringLiteral(": ") + QString::fromLocal8Bit(std::strerror(errno)))};
    if (!file.open(fd, QIODevice::ReadOnly, QFileDevice::AutoCloseHandle)) {
        const QString why = file.errorString(); (void)::close(fd);
        return {{}, {}, error(QStringLiteral("LLM_CACHE_MODEL_READ"), QStringLiteral("The local model could not be read for response caching. The model can still run."), path + QStringLiteral(": ") + why)};
    }
#else
    if (!file.open(QIODevice::ReadOnly)) return {{}, {}, error(QStringLiteral("LLM_CACHE_MODEL_READ"),
        QStringLiteral("The local model could not be read for response caching. The model can still run."), path + QStringLiteral(": ") + file.errorString())};
#endif
#ifdef Q_OS_UNIX
    struct stat opened{};
    if (::fstat(file.handle(), &opened) != 0 || identityFromStat(opened) != before)
        return {{}, {}, error(QStringLiteral("LLM_CACHE_MODEL_CHANGED"),
            QStringLiteral("The local model changed while its cache identity was being opened. The model can still run; retry caching after the file is stable."), path)};
#endif
    QCryptographicHash hash(QCryptographicHash::Sha256);
    while (!canceled->load()) {
        const QByteArray chunk = file.read(1024 * 1024);
        if (chunk.isEmpty()) {
            if (file.error() != QFileDevice::NoError) return {{}, {}, error(QStringLiteral("LLM_CACHE_MODEL_READ"),
                QStringLiteral("The local model could not be read completely for response caching. The model can still run."), path + QStringLiteral(": ") + file.errorString())};
            break;
        }
        hash.addData(chunk);
    }
    if (canceled->load()) return {};
#ifdef Q_OS_UNIX
    struct stat completed{};
    if (::fstat(file.handle(), &completed) != 0 || identityFromStat(completed) != before)
        return {{}, {}, error(QStringLiteral("LLM_CACHE_MODEL_CHANGED"),
            QStringLiteral("The local model changed while its cache identity was being read. The model can still run; retry caching after the file is stable."), path)};
#endif
    if (assetIdentity(path) != before) return {{}, {}, error(QStringLiteral("LLM_CACHE_MODEL_CHANGED"),
        QStringLiteral("The local model changed while its cache identity was being read. The model can still run; retry caching after the file is stable."), path)};
    const QByteArray digest = hash.result().toHex();
    if (m_verifiedAssets.size() >= 64) m_verifiedAssets.clear();
    m_verifiedAssets.insert(before, digest);
    return {before, digest, {}};
}

struct ModelResponseCache::Lookup ModelResponseCache::lookup(const QByteArray &key) const {
    Q_ASSERT(key.size() == 64);
    if (const QVariantMap failure = prepare(m_parentDirectory, m_directory); !failure.isEmpty()) return {{}, failure};
    QLockFile lock(QDir(m_directory).filePath(QStringLiteral(".lock")));
    lock.setStaleLockTime(30000);
    if (!lock.tryLock(1500)) return {{}, lockError(m_directory, lock)};
    if (const QVariantMap failure = pruneExpired(m_directory); !failure.isEmpty()) return {{}, failure};
    const QString path = pathFor(m_directory, key);
    const QFileInfo info(path);
    if (!info.exists() && !info.isSymLink()) return {};
    if (info.isSymLink() || !info.isFile()) return {{}, error(QStringLiteral("LLM_CACHE_FILE"),
        QStringLiteral("A cached response has an unsafe file type. Remove or repair it before caching this request."), path)};
    if (info.size() > MaxEntryBytes || info.size() < 0) {
        const QVariantMap removal = removeFile(info);
        return {{}, removal.isEmpty() ? error(QStringLiteral("LLM_CACHE_DAMAGED"), QStringLiteral("A cached response was too large and was discarded. The model will run again."), path) : removal};
    }
    QFile file(path);
#ifdef Q_OS_UNIX
    const QByteArray native = QFile::encodeName(path);
    const int fd = ::open(native.constData(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
    if (fd < 0) return {{}, error(QStringLiteral("LLM_CACHE_READ"), QStringLiteral("A cached response could not be opened. Check cache permissions; the model will still run."), path + QStringLiteral(": ") + QString::fromLocal8Bit(std::strerror(errno)))};
    struct stat attributes{};
    QString securityFailure;
    if (::fstat(fd, &attributes) != 0) securityFailure = QString::fromLocal8Bit(std::strerror(errno));
    else if (!S_ISREG(attributes.st_mode)) securityFailure = QStringLiteral("The cache entry is not a regular file.");
    else if (attributes.st_uid != ::geteuid()) securityFailure = QStringLiteral("The cache entry is not owned by the current user.");
    else if (attributes.st_nlink != 1) securityFailure = QStringLiteral("The cache entry has multiple hard links.");
    else if ((attributes.st_mode & (S_IRWXG | S_IRWXO)) != 0) securityFailure = QStringLiteral("The cache entry grants access beyond its owner.");
    else if (attributes.st_size > MaxEntryBytes) securityFailure = QStringLiteral("The cache entry exceeds its size limit.");
    if (!securityFailure.isEmpty()) {
        (void)::close(fd);
        return {{}, error(QStringLiteral("LLM_CACHE_FILE"), QStringLiteral("A cached response is not a secure, owner-controlled file. The model will still run."), path + QStringLiteral(": ") + securityFailure)};
    }
    if (!file.open(fd, QIODevice::ReadOnly, QFileDevice::AutoCloseHandle)) {
        const QString why = file.errorString(); (void)::close(fd);
        return {{}, error(QStringLiteral("LLM_CACHE_READ"), QStringLiteral("A cached response could not be read. The model will still run."), path + QStringLiteral(": ") + why)};
    }
#else
    if (!file.open(QIODevice::ReadOnly)) return {{}, error(QStringLiteral("LLM_CACHE_READ"), QStringLiteral("A cached response could not be read. The model will still run."), path + QStringLiteral(": ") + file.errorString())};
#endif
    const QByteArray bytes = file.read(MaxEntryBytes + 1);
    if (file.error() != QFileDevice::NoError) return {{}, error(QStringLiteral("LLM_CACHE_READ"), QStringLiteral("A cached response could not be read. The model will still run."), path + QStringLiteral(": ") + file.errorString())};
    QJsonParseError parse{};
    const QJsonDocument doc = QJsonDocument::fromJson(bytes, &parse);
    const QJsonObject root = doc.object();
    const double savedNumber = root.value(QStringLiteral("savedAt")).toDouble(-1);
    const bool validTime = root.value(QStringLiteral("savedAt")).isDouble() && std::isfinite(savedNumber)
        && savedNumber >= 0 && savedNumber <= 253402300799999.0 && std::floor(savedNumber) == savedNumber;
    const qint64 savedAt = validTime ? static_cast<qint64>(savedNumber) : -1;
    const QJsonObject message = root.value(QStringLiteral("message")).toObject();
    const QByteArray checksum = QCryptographicHash::hash(QJsonDocument(message).toJson(QJsonDocument::Compact), QCryptographicHash::Sha256).toHex();
    const bool valid = bytes.size() <= MaxEntryBytes && parse.error == QJsonParseError::NoError && doc.isObject()
        && root.value(QStringLiteral("version")).toInt() == 1 && root.value(QStringLiteral("key")).toString().toLatin1() == key
        && validTime && root.value(QStringLiteral("message")).isObject() && validMessage(message)
        && root.value(QStringLiteral("checksum")).isString() && root.value(QStringLiteral("checksum")).toString().toLatin1() == checksum;
    if (!valid) {
        file.close();
        const QVariantMap removal = removeFile(info);
        return {{}, removal.isEmpty() ? error(QStringLiteral("LLM_CACHE_DAMAGED"), QStringLiteral("A cached response was damaged and was discarded. The model will run again."), path + QStringLiteral(": ") + (parse.error == QJsonParseError::NoError ? QStringLiteral("Invalid cache metadata or message structure.") : parse.errorString())) : removal};
    }
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const qint64 lastUsed = info.lastModified().toMSecsSinceEpoch();
    if (!info.lastModified().isValid() || savedAt > now || lastUsed > now) {
        file.close();
        return {};
    }
    if (now - savedAt >= ExpiryMs || now - lastUsed >= ExpiryMs) {
        file.close();
        if (const QVariantMap removal = removeFile(info); !removal.isEmpty()) return {{}, removal};
        return {};
    }
    const QByteArray token = QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex();
    if (!file.setFileTime(QDateTime::currentDateTimeUtc(), QFileDevice::FileModificationTime))
        return {message, error(QStringLiteral("LLM_CACHE_ACCESS_TIME"), QStringLiteral("A cached response was read, but its recency could not be saved. Check cache permissions."), path + QStringLiteral(": ") + file.errorString()), token};
    return {message, {}, token};
}

QVariantMap ModelResponseCache::store(const QByteArray &key, const QJsonObject &message) const {
    Q_ASSERT(key.size() == 64);
    if (!validMessage(message)) return error(QStringLiteral("LLM_CACHE_RESPONSE"), QStringLiteral("The model response could not be cached because its structure was invalid. The response remains available."), {});
    if (const QVariantMap failure = prepare(m_parentDirectory, m_directory); !failure.isEmpty()) return failure;
    QLockFile lock(QDir(m_directory).filePath(QStringLiteral(".lock")));
    lock.setStaleLockTime(30000);
    if (!lock.tryLock(1500)) return lockError(m_directory, lock);
    if (const QVariantMap failure = pruneExpired(m_directory); !failure.isEmpty()) return failure;
    const QString path = pathFor(m_directory, key);
    const QFileInfo target(path);
    if (target.isSymLink() || (target.exists() && !target.isFile())) return error(QStringLiteral("LLM_CACHE_FILE"),
        QStringLiteral("A cached response has an unsafe file type. Remove or repair it before caching this request."), path);
    const QByteArray checksum = QCryptographicHash::hash(QJsonDocument(message).toJson(QJsonDocument::Compact), QCryptographicHash::Sha256).toHex();
    const QJsonObject root{{QStringLiteral("version"), 1}, {QStringLiteral("key"), QString::fromLatin1(key)},
        {QStringLiteral("savedAt"), static_cast<double>(QDateTime::currentMSecsSinceEpoch())},
        {QStringLiteral("checksum"), QString::fromLatin1(checksum)}, {QStringLiteral("message"), message}};
    const QByteArray bytes = QJsonDocument(root).toJson(QJsonDocument::Compact);
    if (bytes.size() > MaxEntryBytes) return error(QStringLiteral("LLM_CACHE_LIMIT"),
        QStringLiteral("The model response is too large to cache. The response remains available."),
        QStringLiteral("%1: %2 bytes; limit %3.").arg(path).arg(bytes.size()).arg(MaxEntryBytes));
    if (const QVariantMap failure = prune(m_directory, target.fileName(), bytes.size()); !failure.isEmpty()) return failure;
    QSaveFile file(path);
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly)) return error(QStringLiteral("LLM_CACHE_WRITE"), QStringLiteral("The model response could not be cached. Check cache storage and permissions."), path + QStringLiteral(": ") + file.errorString());
    if (!file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner)) { file.cancelWriting(); return error(QStringLiteral("LLM_CACHE_SECURITY"), QStringLiteral("The model response cache file could not be secured. Check cache permissions."), path + QStringLiteral(": ") + file.errorString()); }
    if (file.write(bytes) != bytes.size()) { const QString why = file.errorString(); file.cancelWriting(); return error(QStringLiteral("LLM_CACHE_WRITE"), QStringLiteral("The model response could not be cached. Check cache storage and permissions."), path + QStringLiteral(": ") + why); }
    if (!file.commit()) return error(QStringLiteral("LLM_CACHE_WRITE"), QStringLiteral("The model response could not be cached. Check cache storage and permissions."), path + QStringLiteral(": ") + file.errorString());
    return {};
}

QVariantMap ModelResponseCache::discard(const QByteArray &key, const QByteArray &token) const {
    Q_ASSERT(key.size() == 64 && token.size() == 64);
    if (const QVariantMap failure = prepare(m_parentDirectory, m_directory); !failure.isEmpty()) return failure;
    QLockFile lock(QDir(m_directory).filePath(QStringLiteral(".lock")));
    lock.setStaleLockTime(30000);
    if (!lock.tryLock(1500)) return lockError(m_directory, lock);
    const QFileInfo target(pathFor(m_directory, key));
    if (!target.exists() && !target.isSymLink()) return {};
    if (target.isSymLink() || !target.isFile()) return error(QStringLiteral("LLM_CACHE_FILE"),
        QStringLiteral("A cached response has an unsafe file type. Remove or repair it before caching this request."), target.absoluteFilePath());
    if (target.size() > MaxEntryBytes) return error(QStringLiteral("LLM_CACHE_FILE"),
        QStringLiteral("A cached response changed before it could be discarded. Inspect the cache directory."), target.absoluteFilePath());
    QFile file(target.absoluteFilePath());
#ifdef Q_OS_UNIX
    const QByteArray native = QFile::encodeName(target.absoluteFilePath());
    const int fd = ::open(native.constData(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
    if (fd < 0) return error(QStringLiteral("LLM_CACHE_READ"), QStringLiteral("A cached response could not be checked before removal."),
        target.absoluteFilePath() + QStringLiteral(": ") + QString::fromLocal8Bit(std::strerror(errno)));
    struct stat attributes{};
    QString securityFailure;
    if (::fstat(fd, &attributes) != 0) securityFailure = QString::fromLocal8Bit(std::strerror(errno));
    else if (!S_ISREG(attributes.st_mode)) securityFailure = QStringLiteral("The cache entry is not a regular file.");
    else if (attributes.st_uid != ::geteuid()) securityFailure = QStringLiteral("The cache entry is not owned by the current user.");
    else if (attributes.st_nlink != 1) securityFailure = QStringLiteral("The cache entry has multiple hard links.");
    else if (attributes.st_size > MaxEntryBytes) securityFailure = QStringLiteral("The cache entry exceeds its size limit.");
    if (!securityFailure.isEmpty()) { (void)::close(fd); return error(QStringLiteral("LLM_CACHE_FILE"),
        QStringLiteral("A cached response could not be checked securely before removal."), target.absoluteFilePath() + QStringLiteral(": ") + securityFailure); }
    if (!file.open(fd, QIODevice::ReadOnly, QFileDevice::AutoCloseHandle)) {
        const QString why = file.errorString(); (void)::close(fd);
        return error(QStringLiteral("LLM_CACHE_READ"), QStringLiteral("A cached response could not be checked before removal."), target.absoluteFilePath() + QStringLiteral(": ") + why);
    }
#else
    if (!file.open(QIODevice::ReadOnly)) return error(QStringLiteral("LLM_CACHE_READ"),
        QStringLiteral("A cached response could not be checked before removal."), target.absoluteFilePath() + QStringLiteral(": ") + file.errorString());
#endif
    const QByteArray bytes = file.read(MaxEntryBytes + 1);
    if (file.error() != QFileDevice::NoError) return error(QStringLiteral("LLM_CACHE_READ"),
        QStringLiteral("A cached response could not be checked before removal."), target.absoluteFilePath() + QStringLiteral(": ") + file.errorString());
    file.close();
    if (QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex() != token) return {};
    return removeFile(target);
}
