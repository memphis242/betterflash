#include "CardChatStore.h"

#include "core/cardmodel.h"

#include <QDir>
#include <QCryptographicHash>
#include <QFile>
#include <QFileDevice>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QLockFile>
#include <QSaveFile>
#include <QSet>
#include <QStringList>
#include <QUrl>

namespace {
constexpr qsizetype maximumFileBytes = 2 * 1024 * 1024;
constexpr qsizetype maximumTextCharacters = 32'000;
constexpr qsizetype maximumCardContextCharacters = 64'000;
constexpr int maximumThreads = 50;
constexpr int maximumMessages = 200;
constexpr int maximumSources = 15;
constexpr qsizetype maximumTitleCharacters = 160;
constexpr qsizetype maximumDeckNameCharacters = 256;
constexpr qsizetype maximumTagCharacters = 4096;
constexpr qsizetype maximumSourceIdCharacters = 32;
constexpr qsizetype maximumSourceTitleCharacters = 300;
constexpr qsizetype maximumSourceUrlCharacters = 2048;

struct StoredSnapshot {
    QJsonArray threads{};
    QByteArray bytes{};
    bool exists = false;
};

std::expected<QString, QString> validateSources(const QJsonValue &value, const QString &location)
{
    if (!value.isArray())
        return std::unexpected(QStringLiteral("%1 must be an array.").arg(location));
    const QJsonArray sources = value.toArray();
    if (sources.size() > maximumSources)
        return std::unexpected(QStringLiteral("%1 contains more than %2 sources.").arg(location).arg(maximumSources));
    for (qsizetype index = 0; index < sources.size(); ++index) {
        const QJsonValue sourceValue = sources.at(index);
        if (!sourceValue.isObject())
            return std::unexpected(QStringLiteral("%1[%2] must be an object.").arg(location, QString::number(index)));
        const QJsonObject source = sourceValue.toObject();
        const QString id = source.value(QStringLiteral("id")).toString();
        const QString title = source.value(QStringLiteral("title")).toString();
        const QJsonValue urlValue = source.value(QStringLiteral("url"));
        const QString urlText = urlValue.toString();
        const QJsonValue sourceCardIdValue = source.value(QStringLiteral("cardId"));
        const QString cardId = sourceCardIdValue.toString();
        if (id.isEmpty() || id.size() > maximumSourceIdCharacters
            || title.isEmpty() || title.size() > maximumSourceTitleCharacters
            || (!sourceCardIdValue.isUndefined() && !betterflash::model::validUuid(cardId))
            || (urlValue.isUndefined() && !betterflash::model::validUuid(cardId))
            || urlText.size() > maximumSourceUrlCharacters) {
            return std::unexpected(QStringLiteral("%1[%2] has a missing or oversized id, title, or source location.").arg(location, QString::number(index)));
        }
        if (!urlValue.isUndefined()) {
            const QUrl url(urlText, QUrl::StrictMode);
            if (!urlValue.isString() || !url.isValid()
                || (url.scheme() != QStringLiteral("http") && url.scheme() != QStringLiteral("https"))
                || url.host().isEmpty() || !url.userInfo().isEmpty()) {
                return std::unexpected(QStringLiteral("%1[%2] contains a URL that is not a safe HTTP or HTTPS source.").arg(location, QString::number(index)));
            }
        }
    }
    return {};
}

std::expected<void, QString> validateThreads(const QString &cardId, const QJsonArray &threads)
{
    if (threads.size() > maximumThreads)
        return std::unexpected(QStringLiteral("A card can have at most %1 saved chats.").arg(maximumThreads));

    QSet<QString> threadIds;
    qsizetype totalCharacters = 0;
    for (qsizetype threadIndex = 0; threadIndex < threads.size(); ++threadIndex) {
        const QJsonValue threadValue = threads.at(threadIndex);
        if (!threadValue.isObject())
            return std::unexpected(QStringLiteral("Chat %1 must be an object.").arg(QString::number(threadIndex + 1)));
        const QJsonObject thread = threadValue.toObject();
        const QString id = thread.value(QStringLiteral("id")).toString();
        if (!betterflash::model::validUuid(id) || threadIds.contains(id))
            return std::unexpected(QStringLiteral("Chat %1 has an invalid or duplicate identifier.").arg(QString::number(threadIndex + 1)));
        threadIds.insert(id);

        const QJsonValue titleValue = thread.value(QStringLiteral("title"));
        const QString title = titleValue.toString();
        if (!titleValue.isString() || title.size() > maximumTitleCharacters)
            return std::unexpected(QStringLiteral("Chat %1 has a missing, nontext, or oversized title (maximum %2 characters).")
                .arg(QString::number(threadIndex + 1)).arg(maximumTitleCharacters));
        totalCharacters += title.size();

        for (const QString &field : {QStringLiteral("createdAt"), QStringLiteral("updatedAt")}) {
            const QJsonValue timestamp = thread.value(field);
            if (!timestamp.isString() || !betterflash::model::utcInstant(timestamp.toString()))
                return std::unexpected(QStringLiteral("Chat %1 has an invalid %2 timestamp.").arg(QString::number(threadIndex + 1), field));
        }

        const QJsonValue contextValue = thread.value(QStringLiteral("context"));
        if (!contextValue.isObject())
            return std::unexpected(QStringLiteral("Chat %1 is missing its card context.").arg(QString::number(threadIndex + 1)));
        const QJsonObject context = contextValue.toObject();
        for (const QString &field : {QStringLiteral("cardId"), QStringLiteral("front"), QStringLiteral("back"),
                                     QStringLiteral("kind"), QStringLiteral("deckName"), QStringLiteral("tags")}) {
            if (!context.value(field).isString())
                return std::unexpected(QStringLiteral("Chat %1 has a missing or nontext %2 context field.").arg(QString::number(threadIndex + 1), field));
        }
        const QString contextCardId = context.value(QStringLiteral("cardId")).toString();
        const QString front = context.value(QStringLiteral("front")).toString();
        const QString back = context.value(QStringLiteral("back")).toString();
        const QString kind = context.value(QStringLiteral("kind")).toString();
        const QString deckName = context.value(QStringLiteral("deckName")).toString();
        const QString tags = context.value(QStringLiteral("tags")).toString();
        if (contextCardId != cardId || front.isEmpty() || front.size() + back.size() > maximumCardContextCharacters
            || (kind != QStringLiteral("basic") && kind != QStringLiteral("reverse") && kind != QStringLiteral("cloze"))
            || deckName.size() > maximumDeckNameCharacters || tags.size() > maximumTagCharacters) {
            return std::unexpected(QStringLiteral("Chat %1 has card context that is invalid or belongs to another card.").arg(QString::number(threadIndex + 1)));
        }
        totalCharacters += front.size() + back.size() + deckName.size() + tags.size();

        const QJsonValue messagesValue = thread.value(QStringLiteral("messages"));
        if (!messagesValue.isArray())
            return std::unexpected(QStringLiteral("Chat %1 is missing its message list.").arg(QString::number(threadIndex + 1)));
        const QJsonArray messages = messagesValue.toArray();
        if (messages.size() > maximumMessages)
            return std::unexpected(QStringLiteral("Chat %1 contains more than %2 messages.").arg(QString::number(threadIndex + 1)).arg(maximumMessages));
        QString expectedRole = QStringLiteral("user");
        for (qsizetype messageIndex = 0; messageIndex < messages.size(); ++messageIndex) {
            const QJsonValue messageValue = messages.at(messageIndex);
            if (!messageValue.isObject())
                return std::unexpected(QStringLiteral("Chat %1 message %2 must be an object.").arg(QString::number(threadIndex + 1), QString::number(messageIndex + 1)));
            const QJsonObject message = messageValue.toObject();
            const QString role = message.value(QStringLiteral("role")).toString();
            const QString content = message.value(QStringLiteral("content")).toString();
            if (role != expectedRole
                || content.isEmpty() || content.size() > maximumTextCharacters) {
                return std::unexpected(QStringLiteral("Chat %1 message %2 has an invalid role order or content length.").arg(QString::number(threadIndex + 1), QString::number(messageIndex + 1)));
            }
            expectedRole = role == QStringLiteral("user") ? QStringLiteral("assistant") : QStringLiteral("user");
            totalCharacters += content.size();
            if (message.contains(QStringLiteral("sources"))) {
                const auto sources = validateSources(message.value(QStringLiteral("sources")),
                    QStringLiteral("Chat %1 message %2 sources").arg(QString::number(threadIndex + 1), QString::number(messageIndex + 1)));
                if (!sources) return std::unexpected(sources.error());
                for (const QJsonValue &sourceValue : message.value(QStringLiteral("sources")).toArray()) {
                    const QJsonObject source = sourceValue.toObject();
                    totalCharacters += source.value(QStringLiteral("id")).toString().size()
                        + source.value(QStringLiteral("title")).toString().size()
                        + source.value(QStringLiteral("url")).toString().size()
                        + source.value(QStringLiteral("cardId")).toString().size();
                }
            }
            if (totalCharacters > maximumFileBytes)
                return std::unexpected(QStringLiteral("Saved chats exceed the %1 character storage limit.").arg(maximumFileBytes));
        }
    }
    return {};
}

QString errorMessage(const QString &operation, const QString &path, const QString &cause)
{
    return QStringLiteral("Could not %1 card chat data at %2. %3. Preserve the file and review its details before retrying.")
        .arg(operation, path, cause);
}

std::expected<struct StoredSnapshot, QString> readSnapshot(const QString &cardId, const QString &path)
{
    const QFileInfo info(path);
    if (info.isSymLink())
        return std::unexpected(errorMessage(QStringLiteral("read"), path,
            QStringLiteral("The chat path is a symbolic link; preserve and inspect it before retrying")));
    if (!info.exists()) return StoredSnapshot{};
    if (!info.isFile() || info.size() < 0 || info.size() > maximumFileBytes)
        return std::unexpected(errorMessage(QStringLiteral("read"), path,
            QStringLiteral("The chat file is not a regular file or exceeds the %1 byte limit").arg(maximumFileBytes)));
    const QFileDevice::Permissions unsafePermissions = QFileDevice::ReadGroup | QFileDevice::WriteGroup
        | QFileDevice::ExeGroup | QFileDevice::ReadOther | QFileDevice::WriteOther | QFileDevice::ExeOther;
    if (info.permissions().testAnyFlags(unsafePermissions))
        return std::unexpected(errorMessage(QStringLiteral("read"), path,
            QStringLiteral("The chat file is accessible to other users; restrict its permissions to the owner")));

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return std::unexpected(errorMessage(QStringLiteral("read"), path, file.errorString()));
    const QByteArray data = file.read(maximumFileBytes + 1);
    if (file.error() != QFileDevice::NoError)
        return std::unexpected(errorMessage(QStringLiteral("read"), path, file.errorString()));
    if (data.size() > maximumFileBytes)
        return std::unexpected(errorMessage(QStringLiteral("read"), path,
            QStringLiteral("The chat file exceeds the %1 byte limit").arg(maximumFileBytes)));

    QJsonParseError parseError{};
    const QJsonDocument document = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
        return std::unexpected(errorMessage(QStringLiteral("parse"), path,
            QStringLiteral("Invalid JSON at byte %1: %2").arg(parseError.offset).arg(parseError.errorString())));
    const QJsonObject root = document.object();
    const QJsonValue version = root.value(QStringLiteral("version"));
    const QJsonValue threadsValue = root.value(QStringLiteral("threads"));
    if (!version.isDouble() || version.toInt(-1) != 1 || !threadsValue.isArray())
        return std::unexpected(errorMessage(QStringLiteral("validate"), path,
            QStringLiteral("The file must contain version 1 and a threads array")));
    const QJsonArray threads = threadsValue.toArray();
    const auto valid = validateThreads(cardId, threads);
    if (!valid)
        return std::unexpected(errorMessage(QStringLiteral("validate"), path, valid.error()));
    return StoredSnapshot{threads, data, true};
}
}

CardChatStore::CardChatStore(const QString &dataDirectory)
    : m_rootDirectory(QDir(dataDirectory).filePath(QStringLiteral("card-chats")))
{
    Q_ASSERT(!dataDirectory.isEmpty());
}

QString CardChatStore::pathFor(const QString &cardId) const
{
    if (!betterflash::model::validUuid(cardId)) return {};
    return QDir(m_rootDirectory).filePath(cardId + QStringLiteral(".json"));
}

std::expected<QJsonArray, QString> CardChatStore::load(const QString &cardId) const
{
    const QString path = pathFor(cardId);
    if (path.isEmpty())
        return std::unexpected(QStringLiteral("Could not load card chats because the card identifier is invalid."));
    const QFileInfo directoryInfo(m_rootDirectory);
    if (directoryInfo.isSymLink())
        return std::unexpected(errorMessage(QStringLiteral("read"), m_rootDirectory,
            QStringLiteral("The chat directory is a symbolic link; preserve and inspect it before retrying")));
    if (directoryInfo.exists() && (!directoryInfo.isDir()
        || directoryInfo.permissions().testAnyFlags(QFileDevice::ReadGroup | QFileDevice::WriteGroup
            | QFileDevice::ExeGroup | QFileDevice::ReadOther | QFileDevice::WriteOther | QFileDevice::ExeOther)))
        return std::unexpected(errorMessage(QStringLiteral("read"), m_rootDirectory,
            QStringLiteral("The chat directory is not a private directory with owner-only permissions")));

    const auto snapshot = readSnapshot(cardId, path);
    if (!snapshot) return std::unexpected(snapshot.error());
    m_baselines.insert(cardId, snapshot->exists
        ? QCryptographicHash::hash(snapshot->bytes, QCryptographicHash::Sha256) : QByteArray{});
    return snapshot->threads;
}

std::expected<void, QString> CardChatStore::save(const QString &cardId, const QJsonArray &threads) const
{
    const QString path = pathFor(cardId);
    if (path.isEmpty())
        return std::unexpected(QStringLiteral("Could not save card chats because the card identifier is invalid."));
    if (!m_baselines.contains(cardId))
        return std::unexpected(QStringLiteral("Could not save card chats because this store has no loaded baseline for the card. Load the current chats and retry."));
    const auto valid = validateThreads(cardId, threads);
    if (!valid) return std::unexpected(valid.error());

    const QJsonObject root{{QStringLiteral("version"), 1}, {QStringLiteral("threads"), threads}};
    const QByteArray data = QJsonDocument(root).toJson(QJsonDocument::Compact);
    if (data.size() > maximumFileBytes)
        return std::unexpected(QStringLiteral("Card chats exceed the %1 byte storage limit.").arg(maximumFileBytes));

    QDir directory(m_rootDirectory);
    if (!directory.exists() && !QDir().mkpath(m_rootDirectory))
        return std::unexpected(errorMessage(QStringLiteral("create the chat directory"), m_rootDirectory,
            QStringLiteral("The directory could not be created")));
    const QFileInfo directoryInfo(m_rootDirectory);
    if (directoryInfo.isSymLink() || !directoryInfo.isDir())
        return std::unexpected(errorMessage(QStringLiteral("save chats"), m_rootDirectory,
            QStringLiteral("The chat directory is not a regular directory")));
    if (!QFile::setPermissions(m_rootDirectory, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner))
        return std::unexpected(errorMessage(QStringLiteral("secure"), m_rootDirectory,
            QStringLiteral("Could not restrict the chat directory to owner-only permissions")));

    QLockFile lock(path + QStringLiteral(".lock"));
    lock.setStaleLockTime(30'000);
    if (!lock.tryLock(0))
        return std::unexpected(errorMessage(QStringLiteral("lock for writing"), path,
            QStringLiteral("Another application instance is saving this card's chats; try again")));

    const auto current = readSnapshot(cardId, path);
    if (!current) return std::unexpected(current.error());
    const QByteArray actualBaseline = current->exists
        ? QCryptographicHash::hash(current->bytes, QCryptographicHash::Sha256) : QByteArray{};
    if (actualBaseline != m_baselines.value(cardId))
        return std::unexpected(errorMessage(QStringLiteral("save"), path,
            QStringLiteral("The saved chats changed after they were loaded. Reload or merge the current chats before saving")));

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return std::unexpected(errorMessage(QStringLiteral("open for writing"), path, file.errorString()));
    if (!file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner)) {
        const QString cause = file.errorString();
        file.cancelWriting();
        return std::unexpected(errorMessage(QStringLiteral("secure"), path, cause));
    }
    if (file.write(data) != data.size()) {
        const QString cause = file.errorString();
        file.cancelWriting();
        return std::unexpected(errorMessage(QStringLiteral("write"), path, cause));
    }
    if (!file.commit())
        return std::unexpected(errorMessage(QStringLiteral("commit"), path, file.errorString()));
    m_baselines.insert(cardId, QCryptographicHash::hash(data, QCryptographicHash::Sha256));
    return {};
}
