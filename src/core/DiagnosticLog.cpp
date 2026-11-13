#include "DiagnosticLog.h"

#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLockFile>
#include <QMetaObject>
#include <QRegularExpression>
#include <QSettings>
#include <QSet>
#include <QUrl>
#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <functional>
#include <mutex>
#include <utility>

#ifdef Q_OS_UNIX
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace {
constexpr qint64 segmentLimit = 4LL * 1024 * 1024;
constexpr qsizetype recordLimit = 1024 * 1024;
constexpr int maxSegments = 16;
constexpr int retentionDays = 7;
static_assert(segmentLimit > recordLimit && maxSegments * segmentLimit == 64LL * 1024 * 1024);
static_assert(retentionDays > 0 && maxSegments > 0);
std::atomic<DiagnosticLog *> currentLogger{nullptr};
std::mutex handlerMutex;
QtMessageHandler previousHandler = nullptr;
thread_local bool insideHandler = false;
thread_local bool insideRecord = false;
struct RecordGuard {
    const bool wasInside = insideRecord;
    RecordGuard() { insideRecord = true; }
    ~RecordGuard() { insideRecord = wasInside; }
};

int severity(const QString &level) {
    if (level == QStringLiteral("debug")) return 0;
    if (level == QStringLiteral("info")) return 1;
    if (level == QStringLiteral("warning")) return 2;
    if (level == QStringLiteral("error")) return 3;
    return -1;
}
bool validVerbosity(const QString &value) {
    return value == QStringLiteral("low") || value == QStringLiteral("normal") || value == QStringLiteral("high");
}
bool sensitiveKey(const QString &key) {
    const QString name = key.toCaseFolded();
    return name == QStringLiteral("authorization") || name == QStringLiteral("proxy-authorization")
        || name == QStringLiteral("x-api-key") || name == QStringLiteral("api_key")
        || name == QStringLiteral("apikey") || name == QStringLiteral("api-key")
        || name == QStringLiteral("password") || name == QStringLiteral("secret")
        || name == QStringLiteral("credential") || name == QStringLiteral("access_token")
        || name == QStringLiteral("refresh_token") || name == QStringLiteral("token");
}
bool normalMetadataKey(const QString &key) {
    static const QSet<QString> allowed{QStringLiteral("code"), QStringLiteral("errorcode"), QStringLiteral("turn"),
        QStringLiteral("attempt"), QStringLiteral("searchcount"), QStringLiteral("requestcount"),
        QStringLiteral("operation"), QStringLiteral("stage"), QStringLiteral("provider"), QStringLiteral("model"),
        QStringLiteral("status"), QStringLiteral("httpstatus"), QStringLiteral("limit"), QStringLiteral("bytes"),
        QStringLiteral("durationms"), QStringLiteral("line"), QStringLiteral("category")};
    return allowed.contains(key.toCaseFolded());
}
void messageHandler(const QtMsgType type, const QMessageLogContext &context, const QString &message) {
    if (insideHandler || insideRecord) {
        static constexpr char notice[] = "Diagnostic logging encountered a nested Qt message.\n";
        (void)std::fwrite(notice, 1, sizeof(notice) - 1, stderr);
        return;
    }
    insideHandler = true;
    {
        const std::lock_guard guard(handlerMutex);
        DiagnosticLog *const logger = currentLogger.load();
        QString level;
        switch (type) {
        case QtDebugMsg: level = QStringLiteral("debug"); break;
        case QtInfoMsg: level = QStringLiteral("info"); break;
        case QtWarningMsg: level = QStringLiteral("warning"); break;
        case QtCriticalMsg:
        case QtFatalMsg: level = QStringLiteral("error"); break;
        }
        if (logger) {
            const QJsonObject details{{QStringLiteral("message"), message},
                {QStringLiteral("category"), QString::fromUtf8(context.category ? context.category : "default")},
                {QStringLiteral("file"), QString::fromUtf8(context.file ? context.file : "")},
                {QStringLiteral("line"), context.line}};
            (void)logger->record(level, QStringLiteral("qt.message"), details);
        }
        if (previousHandler) {
            previousHandler(type, context, logger ? logger->redact(message) : message);
        } else {
            const QString formatted = qFormatLogMessage(type, context, message);
            const QByteArray line = (logger ? logger->redact(formatted) : formatted).toLocal8Bit();
            (void)std::fwrite(line.constData(), 1, static_cast<size_t>(line.size()), stderr);
            (void)std::fputc('\n', stderr);
        }
    }
    insideHandler = false;
}

bool ensureDirectory(const QString &path, QString *detail) {
#ifdef Q_OS_UNIX
    const QByteArray native = QFile::encodeName(path);
    if (::mkdir(native.constData(), S_IRWXU) != 0 && errno != EEXIST) {
        *detail = QString::fromLocal8Bit(std::strerror(errno));
        return false;
    }
    const int descriptor = ::open(native.constData(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (descriptor < 0) { *detail = QString::fromLocal8Bit(std::strerror(errno)); return false; }
    struct stat attributes{};
    bool okay = ::fstat(descriptor, &attributes) == 0;
    if (!okay) *detail = QString::fromLocal8Bit(std::strerror(errno));
    if (okay && attributes.st_uid != ::geteuid()) {
        *detail = QStringLiteral("The log directory is not owned by the current user."); okay = false;
    }
    if (okay && ::fchmod(descriptor, S_IRWXU) != 0) {
        *detail = QString::fromLocal8Bit(std::strerror(errno)); okay = false;
    }
    if (::close(descriptor) != 0 && okay) { *detail = QString::fromLocal8Bit(std::strerror(errno)); okay = false; }
    return okay;
#else
    const QFileInfo existing(path);
    if (existing.isSymLink() || (existing.exists() && !existing.isDir())) {
        *detail = QStringLiteral("The log path is a symbolic link or is not a directory."); return false;
    }
    if (!QDir().mkpath(path) || !QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner)) {
        *detail = QStringLiteral("Could not create or restrict the log directory."); return false;
    }
    return true;
#endif
}

const QRegularExpression &logFilename() {
    static const QRegularExpression expression(QStringLiteral("^betterflash-(\\d{4}-\\d{2}-\\d{2})-(\\d{2})\\.jsonl$"));
    return expression;
}
struct RetentionError {
    QString code{};
    QString message{};
    QString detail{};
};
struct RetentionError lockError(const QLockFile &lockFile, const QString &path) {
    switch (lockFile.error()) {
    case QLockFile::LockFailedError:
        return {QStringLiteral("LOG_LOCK"), QStringLiteral("Could not lock diagnostic logs because the lock is already held. Close another app instance or retry shortly."), path + QStringLiteral(": lock already held or not removable")};
    case QLockFile::PermissionError:
        return {QStringLiteral("LOG_LOCK"), QStringLiteral("Could not create the diagnostic log lock. Check the log directory permissions."), path + QStringLiteral(": permission denied")};
    case QLockFile::UnknownError:
        return {QStringLiteral("LOG_LOCK"), QStringLiteral("Could not create the diagnostic log lock. Check storage and retry."), path + QStringLiteral(": the cause was not provided")};
    case QLockFile::NoError:
        return {QStringLiteral("LOG_LOCK"), QStringLiteral("Could not lock diagnostic logs. Check storage and retry."), path + QStringLiteral(": no lock error was provided")};
    }
    return {QStringLiteral("LOG_LOCK"), QStringLiteral("Could not lock diagnostic logs. Check storage and retry."), path + QStringLiteral(": unknown lock error %1").arg(static_cast<int>(lockFile.error()))};
}
bool collectRetained(const QString &directory, const QDate &today, QList<QFileInfo> *retained, struct RetentionError *error) {
    Q_ASSERT(retained && error);
    retained->clear();
    const auto entries = QDir(directory).entryInfoList({QStringLiteral("betterflash-*.jsonl")}, QDir::Files | QDir::System | QDir::NoDotAndDotDot, QDir::Name);
    for (const QFileInfo &file : entries) {
        const auto match = logFilename().match(file.fileName());
        if (!match.hasMatch()) continue;
        const QDate date = QDate::fromString(match.captured(1), Qt::ISODate);
        if (!date.isValid()) continue;
        if (file.isSymLink() || !file.isFile()) {
            *error = {QStringLiteral("LOG_FILE_TYPE"), QStringLiteral("A diagnostic log path is not a regular file. Inspect the log directory."), file.absoluteFilePath()}; return false;
        }
        if (date < today.addDays(1 - retentionDays)) {
            QFile expired(file.absoluteFilePath());
            if (!expired.remove()) {
                *error = {QStringLiteral("LOG_ROTATE"), QStringLiteral("Could not remove an expired diagnostic log. Check log directory permissions."), file.absoluteFilePath() + QStringLiteral(": ") + expired.errorString()}; return false;
            }
        } else {
            if (file.size() > segmentLimit) {
                *error = {QStringLiteral("LOG_FILE_SIZE"), QStringLiteral("A diagnostic log exceeds its size limit. Inspect the log directory."), file.absoluteFilePath() + QStringLiteral(": %1 bytes").arg(file.size())}; return false;
            }
            retained->append(file);
        }
    }
    std::sort(retained->begin(), retained->end(), [](const QFileInfo &left, const QFileInfo &right) {
        return left.lastModified() == right.lastModified() ? left.fileName() < right.fileName() : left.lastModified() < right.lastModified();
    });
    while (retained->size() > maxSegments) {
        const QFileInfo oldest = retained->takeFirst();
        QFile expired(oldest.absoluteFilePath());
        if (!expired.remove()) {
            *error = {QStringLiteral("LOG_ROTATE"), QStringLiteral("Could not rotate diagnostic logs. Check log directory permissions."), oldest.absoluteFilePath() + QStringLiteral(": ") + expired.errorString()}; return false;
        }
    }
    return true;
}
}

DiagnosticLog::DiagnosticLog(const QString &dataDirectory, QObject *parent)
    : QObject(parent), m_directory(QDir(dataDirectory).filePath(QStringLiteral("logs"))) {
    QSettings settings;
    const QString configuredLevel = settings.value(QStringLiteral("diagnostics/level"), QStringLiteral("warning")).toString();
    const QString configuredVerbosity = settings.value(QStringLiteral("diagnostics/verbosity"), QStringLiteral("high")).toString();
    if (severity(configuredLevel) >= 0) m_level = configuredLevel;
    if (validVerbosity(configuredVerbosity)) m_verbosity = configuredVerbosity;
    QString detail;
    if (!ensureDirectory(m_directory, &detail)) {
        setErrorUnlocked(QStringLiteral("LOG_DIRECTORY"), QStringLiteral("Could not create or secure the diagnostic log directory. Check its path and permissions."), detail);
    } else {
        QLockFile lockFile(QDir(m_directory).filePath(QStringLiteral(".diagnostics.lock")));
        lockFile.setStaleLockTime(30000);
        if (!lockFile.tryLock(2000)) {
            const struct RetentionError error = lockError(lockFile, QDir(m_directory).filePath(QStringLiteral(".diagnostics.lock")));
            setErrorUnlocked(error.code, error.message, error.detail);
        } else {
            QList<QFileInfo> retained;
            struct RetentionError error{};
            if (!collectRetained(m_directory, QDateTime::currentDateTimeUtc().date(), &retained, &error))
                setErrorUnlocked(error.code, error.message, error.detail);
        }
    }
    const std::lock_guard guard(handlerMutex);
    Q_ASSERT(currentLogger.load() == nullptr);
    currentLogger.store(this);
    m_previousHandler = qInstallMessageHandler(messageHandler);
    previousHandler = m_previousHandler;
}
DiagnosticLog::~DiagnosticLog() {
    const std::lock_guard guard(handlerMutex);
    (void)qInstallMessageHandler(m_previousHandler);
    currentLogger.store(nullptr);
    previousHandler = nullptr;
}
DiagnosticLog *DiagnosticLog::instance() { return currentLogger.load(); }
QString DiagnosticLog::level() const { const QMutexLocker lock(&m_mutex); return m_level; }
QString DiagnosticLog::verbosity() const { const QMutexLocker lock(&m_mutex); return m_verbosity; }
QString DiagnosticLog::lastLogPath() const { const QMutexLocker lock(&m_mutex); return m_lastLogPath; }
QVariantMap DiagnosticLog::errorInfo() const { const QMutexLocker lock(&m_mutex); return m_errorInfo; }
void DiagnosticLog::notifyChanged() { (void)QMetaObject::invokeMethod(this, [this] { emit changed(); }, Qt::QueuedConnection); }
void DiagnosticLog::setErrorUnlocked(const QString &code, const QString &message, const QString &detail) {
    const QVariantMap error{{QStringLiteral("code"), code}, {QStringLiteral("message"), message},
        {QStringLiteral("detail"), QStringLiteral("%1: %2").arg(m_directory, redactUnlocked(detail).left(1000))}};
    if (error == m_errorInfo || m_pendingErrors.contains(error)) return;
    const QByteArray line = (code + QStringLiteral(": ") + message + QStringLiteral(" ") + error.value(QStringLiteral("detail")).toString()).toLocal8Bit();
    (void)std::fwrite(line.constData(), 1, static_cast<size_t>(line.size()), stderr);
    (void)std::fputc('\n', stderr);
    if (m_errorInfo.isEmpty()) {
        m_errorInfo = error;
    } else if (m_pendingErrors.size() < 15) {
        m_pendingErrors.append(error);
    } else if (m_pendingErrors.size() == 15) {
        m_pendingErrors.append(QVariantMap{{QStringLiteral("code"), QStringLiteral("LOG_ERROR_OVERFLOW")},
            {QStringLiteral("message"), QStringLiteral("Additional diagnostic logging errors occurred. Inspect the application stderr output.")},
            {QStringLiteral("detail"), m_directory}});
    }
    notifyChanged();
}
void DiagnosticLog::setLevel(const QString &level) {
    const struct RecordGuard guard{};
    const QMutexLocker lock(&m_mutex);
    if (severity(level) < 0) {
        setErrorUnlocked(QStringLiteral("LOG_LEVEL"), QStringLiteral("The selected log level is invalid. Choose a listed level."), level.left(100)); return;
    }
    if (m_level == level) return;
    QSettings settings;
    settings.setValue(QStringLiteral("diagnostics/level"), level);
    settings.sync();
    if (settings.status() != QSettings::NoError) {
        setErrorUnlocked(QStringLiteral("LOG_SETTINGS"), QStringLiteral("Could not save the log level. Check settings file permissions."), settings.fileName()); return;
    }
    m_level = level;
    notifyChanged();
}
void DiagnosticLog::setVerbosity(const QString &verbosity) {
    const struct RecordGuard guard{};
    const QMutexLocker lock(&m_mutex);
    if (!validVerbosity(verbosity)) {
        setErrorUnlocked(QStringLiteral("LOG_VERBOSITY"), QStringLiteral("The selected log detail is invalid. Choose a listed option."), verbosity.left(100)); return;
    }
    if (m_verbosity == verbosity) return;
    QSettings settings;
    settings.setValue(QStringLiteral("diagnostics/verbosity"), verbosity);
    settings.sync();
    if (settings.status() != QSettings::NoError) {
        setErrorUnlocked(QStringLiteral("LOG_SETTINGS"), QStringLiteral("Could not save the log detail. Check settings file permissions."), settings.fileName()); return;
    }
    m_verbosity = verbosity;
    notifyChanged();
}
void DiagnosticLog::addSecret(const QString &secret) {
    if (secret.isEmpty()) return;
    const QMutexLocker lock(&m_mutex);
    if (!m_secrets.contains(secret)) m_secrets.append(secret);
}
namespace {
QString decodeJsonEscapes(const QString &source) {
    QString decoded;
    decoded.reserve(source.size());
    const auto hexDigit = [](const QChar c) -> int {
        const ushort n = c.unicode();
        if (n >= u'0' && n <= u'9') return int(n - u'0');
        if (n >= u'a' && n <= u'f') return int(n - u'a') + 10;
        if (n >= u'A' && n <= u'F') return int(n - u'A') + 10;
        return -1;
    };
    for (qsizetype index = 0; index < source.size(); ++index) {
        const QChar current = source.at(index);
        if (current != u'\\' || index + 1 >= source.size()) { decoded.append(current); continue; }
        const QChar next = source.at(index + 1);
        switch (next.unicode()) {
        case u'\\': decoded.append(u'\\'); ++index; break;
        case u'"': decoded.append(u'"'); ++index; break;
        case u'/': decoded.append(u'/'); ++index; break;
        case u'b': decoded.append(u'\b'); ++index; break;
        case u'f': decoded.append(u'\f'); ++index; break;
        case u'n': decoded.append(u'\n'); ++index; break;
        case u'r': decoded.append(u'\r'); ++index; break;
        case u't': decoded.append(u'\t'); ++index; break;
        case u'u': {
            if (index + 5 >= source.size()) { decoded.append(current); break; }
            int number = 0;
            bool valid = true;
            for (int offset = 2; offset <= 5; ++offset) {
                const int digit = hexDigit(source.at(index + offset));
                if (digit < 0) { valid = false; break; }
                number = number * 16 + digit;
            }
            if (!valid) { decoded.append(current); break; }
            decoded.append(QChar(static_cast<ushort>(number)));
            index += 5;
            break;
        }
        default: decoded.append(current); break;
        }
    }
    return decoded;
}
}

QString DiagnosticLog::redactUnlocked(QString value) const {
    for (const QString &secret : m_secrets) {
        QStringList variants{secret};
        QStringList frontier{secret};
        for (int depth = 0; depth < 4 && !frontier.isEmpty(); ++depth) {
            QStringList next;
            for (const QString &source : std::as_const(frontier)) {
                const QByteArray encoded = QJsonDocument(QJsonArray{source}).toJson(QJsonDocument::Compact);
                Q_ASSERT(encoded.size() >= 4);
                const QString escaped = QString::fromUtf8(encoded.mid(2, encoded.size() - 4));
                QString htmlSafe = escaped;
                htmlSafe.replace(u'<', QStringLiteral("\\u003c"));
                htmlSafe.replace(u'>', QStringLiteral("\\u003e"));
                for (const QString &candidate : {escaped, htmlSafe}) {
                    if (candidate.size() > 1024 * 1024 || variants.contains(candidate)) continue;
                    variants.append(candidate);
                    next.append(candidate);
                }
            }
            frontier = next;
        }
        std::sort(variants.begin(), variants.end(), [](const QString &left, const QString &right) { return left.size() > right.size(); });
        for (const QString &variant : std::as_const(variants)) value.replace(variant, QStringLiteral("[redacted]"));
    }
    static const QRegularExpression bearer(QStringLiteral("\\b(Bearer\\s+)[A-Za-z0-9._~+/-]+=*"), QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression named(QStringLiteral("(\\b(?:api[_-]?key|authorization|access[_-]?token|refresh[_-]?token|password|secret)\\b[\\\"']?\\s*[:=]\\s*[\\\"']?)[^\\\"'\\s,}]+"), QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression known(QStringLiteral("(?<![A-Za-z0-9_-])(?:sk-ant-|gsk_|tvly-|sk-)[A-Za-z0-9_-]{12,}"), QRegularExpression::CaseInsensitiveOption);
    if (value.contains(u'\\')) {
        constexpr qsizetype maximumScanCharacters = 1024 * 1024;
        if (value.size() > maximumScanCharacters) return QStringLiteral("[redacted: encoded content exceeds credential scan limit]");
        QString decoded = value;
        for (int pass = 0; pass < 8; ++pass) {
            const QString next = decodeJsonEscapes(decoded);
            if (next == decoded) break;
            for (const QString &secret : m_secrets)
                if (next.contains(secret)) return QStringLiteral("[redacted: encoded credential]");
            if (bearer.match(next).hasMatch() || named.match(next).hasMatch() || known.match(next).hasMatch())
                return QStringLiteral("[redacted: encoded credential-like content]");
            decoded = next;
        }
    }
    value.replace(bearer, QStringLiteral("\\1[redacted]"));
    value.replace(named, QStringLiteral("\\1[redacted]"));
    value.replace(known, QStringLiteral("[redacted]"));
    return value;
}
QString DiagnosticLog::redact(const QString &value) const { const QMutexLocker lock(&m_mutex); return redactUnlocked(value); }

QString DiagnosticLog::record(const QString &level, const QString &event, const QJsonObject &details) {
    Q_ASSERT(!insideRecord);
    const struct RecordGuard guard{};
    const QMutexLocker lock(&m_mutex);
    const int eventSeverity = severity(level);
    if (eventSeverity < 0) {
        setErrorUnlocked(QStringLiteral("LOG_EVENT_LEVEL"), QStringLiteral("Could not record a diagnostic event because its level is invalid."), level.left(100));
        return {};
    }
    if (eventSeverity < severity(m_level)) return {};
    const auto sanitize = [this](const QJsonObject &source) {
        if (m_verbosity == QStringLiteral("low")) return QJsonObject{};
        if (m_verbosity == QStringLiteral("normal")) {
            QJsonObject result;
            for (auto it = source.constBegin(); it != source.constEnd(); ++it) {
                if (!normalMetadataKey(it.key()) || sensitiveKey(it.key()) || it.value().isArray() || it.value().isObject()) continue;
                result.insert(it.key(), it.value().isString() ? QJsonValue(redactUnlocked(it.value().toString()).left(256)) : it.value());
            }
            return result;
        }
        std::function<QJsonValue(const QJsonValue &, const QString &, int)> clean;
        clean = [this, &clean](const QJsonValue &value, const QString &key, const int depth) -> QJsonValue {
            if (sensitiveKey(key)) return QStringLiteral("[redacted]");
            if (depth > 32) return QStringLiteral("[nesting limit]");
            if (value.isString()) {
                const QString text = redactUnlocked(value.toString());
                return text;
            }
            if (value.isArray()) {
                QJsonArray result;
                for (const QJsonValue &entry : value.toArray()) result.append(clean(entry, {}, depth + 1));
                return result;
            }
            if (value.isObject()) {
                QJsonObject result;
                const QJsonObject object = value.toObject();
                for (auto it = object.constBegin(); it != object.constEnd(); ++it) result.insert(redactUnlocked(it.key()), clean(it.value(), it.key(), depth + 1));
                return result;
            }
            return value;
        };
        return clean(source, {}, 0).toObject();
    };
    const QDateTime now = QDateTime::currentDateTimeUtc();
    QJsonObject entry{{QStringLiteral("timestamp"), now.toString(Qt::ISODateWithMs)},
        {QStringLiteral("level"), level}, {QStringLiteral("event"), redactUnlocked(event).left(256)},
        {QStringLiteral("details"), sanitize(details)}};
    QByteArray line = QJsonDocument(entry).toJson(QJsonDocument::Compact);
    if (line.size() + 1 > recordLimit) {
        const QByteArray full = line;
        qsizetype prefixLength = std::min<qsizetype>(full.size(), recordLimit / 2);
        do {
            entry.insert(QStringLiteral("details"), QJsonObject{{QStringLiteral("truncated"), true},
                {QStringLiteral("originalBytes"), static_cast<double>(full.size())},
                {QStringLiteral("recordPrefix"), QString::fromUtf8(full.first(prefixLength))}});
            line = QJsonDocument(entry).toJson(QJsonDocument::Compact);
            prefixLength /= 2;
        } while (line.size() + 1 > recordLimit && prefixLength > 0);
    }
    Q_ASSERT(line.size() + 1 <= recordLimit);
    line.append('\n');
    QString detail;
    if (!ensureDirectory(m_directory, &detail)) {
        setErrorUnlocked(QStringLiteral("LOG_DIRECTORY"), QStringLiteral("Could not create or secure the diagnostic log directory. Check its path and permissions."), detail); return {};
    }
    QLockFile lockFile(QDir(m_directory).filePath(QStringLiteral(".diagnostics.lock")));
    lockFile.setStaleLockTime(30000);
    if (!lockFile.tryLock(2000)) {
        const struct RetentionError error = lockError(lockFile, QDir(m_directory).filePath(QStringLiteral(".diagnostics.lock")));
        setErrorUnlocked(error.code, error.message, error.detail); return {};
    }
    const QString today = now.date().toString(Qt::ISODate);
    QList<QFileInfo> retained;
    struct RetentionError retentionError{};
    if (!collectRetained(m_directory, now.date(), &retained, &retentionError)) {
        setErrorUnlocked(retentionError.code, retentionError.message, retentionError.detail); return {};
    }
    QString path;
    int sequence = -1;
    for (const QFileInfo &candidate : std::as_const(retained)) {
        const auto match = logFilename().match(candidate.fileName());
        if (match.captured(1) != today) continue;
        sequence = match.captured(2).toInt();
        path = candidate.size() + line.size() <= segmentLimit ? candidate.absoluteFilePath() : QString();
    }
    if (path.isEmpty()) {
        while (retained.size() >= maxSegments) {
            const QFileInfo oldest = retained.takeFirst();
            QFile expired(oldest.absoluteFilePath());
            if (!expired.remove()) {
                setErrorUnlocked(QStringLiteral("LOG_ROTATE"), QStringLiteral("Could not rotate diagnostic logs. Check log directory permissions."), oldest.absoluteFilePath() + QStringLiteral(": ") + expired.errorString()); return {};
            }
        }
        for (int offset = 1; offset <= 100; ++offset) {
            const int candidate = (sequence + offset) % 100;
            const QString proposed = QDir(m_directory).filePath(QStringLiteral("betterflash-%1-%2.jsonl").arg(today).arg(candidate, 2, 10, QLatin1Char('0')));
            if (!QFileInfo::exists(proposed) && !QFileInfo(proposed).isSymLink()) { path = proposed; break; }
        }
        if (path.isEmpty()) {
            setErrorUnlocked(QStringLiteral("LOG_ROTATE"), QStringLiteral("Could not create a diagnostic log segment. Inspect the log directory."), QStringLiteral("No daily log segment slot is available.")); return {};
        }
    }
    QFile file(path);
#ifdef Q_OS_UNIX
    const QByteArray native = QFile::encodeName(path);
    const int descriptor = ::open(native.constData(), O_WRONLY | O_APPEND | O_CREAT | O_NOFOLLOW | O_CLOEXEC, S_IRUSR | S_IWUSR);
    if (descriptor < 0) {
        setErrorUnlocked(QStringLiteral("LOG_OPEN"), QStringLiteral("Could not open the diagnostic log. Check log directory permissions."),
                         path + QStringLiteral(": ") + QString::fromLocal8Bit(std::strerror(errno))); return {};
    }
    struct stat attributes{};
    QString securityError;
    if (::fstat(descriptor, &attributes) != 0) securityError = QString::fromLocal8Bit(std::strerror(errno));
    else if (!S_ISREG(attributes.st_mode)) securityError = QStringLiteral("The log is not a regular file.");
    else if (attributes.st_uid != ::geteuid()) securityError = QStringLiteral("The log is not owned by the current user.");
    else if (attributes.st_nlink != 1) securityError = QStringLiteral("The log has multiple hard links.");
    else if (::fchmod(descriptor, S_IRUSR | S_IWUSR) != 0) securityError = QString::fromLocal8Bit(std::strerror(errno));
    if (!securityError.isEmpty()) {
        (void)::close(descriptor);
        setErrorUnlocked(QStringLiteral("LOG_FILE_SECURITY"), QStringLiteral("Could not secure the diagnostic log file. Inspect its ownership and permissions."), path + QStringLiteral(": ") + securityError); return {};
    }
    if (!file.open(descriptor, QIODevice::WriteOnly | QIODevice::Append | QIODevice::Unbuffered, QFileDevice::AutoCloseHandle)) {
        const QString failure = file.errorString();
        (void)::close(descriptor);
        setErrorUnlocked(QStringLiteral("LOG_OPEN"), QStringLiteral("Could not open the diagnostic log. Check log directory permissions."), path + QStringLiteral(": ") + failure); return {};
    }
#else
    if (QFileInfo(path).isSymLink() || !file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Unbuffered)
        || !file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner)) {
        setErrorUnlocked(QStringLiteral("LOG_OPEN"), QStringLiteral("Could not open or secure the diagnostic log. Check log directory permissions."), path + QStringLiteral(": ") + file.errorString()); return {};
    }
#endif
    const qint64 originalSize = file.size();
    if (originalSize < 0) {
        setErrorUnlocked(QStringLiteral("LOG_FILE_SIZE"), QStringLiteral("Could not read the diagnostic log size. Check the log file and storage."), path + QStringLiteral(": ") + file.errorString()); return {};
    }
    const qint64 written = file.write(line);
    if (written != line.size() || !file.flush()) {
        const QString failure = QStringLiteral("Wrote %1 of %2 bytes: %3").arg(written).arg(line.size()).arg(file.errorString());
        const bool restored = file.resize(originalSize);
        setErrorUnlocked(QStringLiteral("LOG_WRITE"), QStringLiteral("Could not write the diagnostic log. Check available space and permissions."),
                         path + QStringLiteral(": ") + failure + (restored ? QString() : QStringLiteral(" The incomplete record could not be removed."))); return {};
    }
    if (m_lastLogPath != path) {
        m_lastLogPath = path;
        notifyChanged();
    }
    return path;
}
void DiagnosticLog::openLogDirectory() {
    if (QDesktopServices::openUrl(QUrl::fromLocalFile(m_directory))) return;
    const QMutexLocker lock(&m_mutex);
    setErrorUnlocked(QStringLiteral("LOG_OPEN_DIRECTORY"), QStringLiteral("Could not open the diagnostic log directory. Open it in your file manager."), m_directory);
}

void DiagnosticLog::acknowledgeError() {
    const struct RecordGuard guard{};
    const QMutexLocker lock(&m_mutex);
    if (m_errorInfo.isEmpty()) return;
    m_errorInfo = m_pendingErrors.isEmpty() ? QVariantMap{} : m_pendingErrors.takeFirst();
    notifyChanged();
}
