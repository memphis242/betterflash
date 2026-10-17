#include "deckcheckpoints.h"
#include "databaseworker.h"
#include "mediabackup.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QMap>
#include <QSqlError>
#include <QSqlQuery>
#include <algorithm>
#include <cassert>
#include <cmath>

namespace model = betterflash::model;
namespace checkpoints = betterflash::checkpoints;

namespace {
QByteArray encoded(const QVariantMap &value)
{
    return QJsonDocument(QJsonObject::fromVariantMap(value)).toJson(QJsonDocument::Compact);
}

bool isMap(const QVariant &value)
{
    return value.metaType().id() == QMetaType::QVariantMap;
}

bool isList(const QVariant &value)
{
    return value.metaType().id() == QMetaType::QVariantList;
}

bool integer(const QVariant &value, qint64 minimum, qint64 maximum)
{
    switch (value.metaType().id()) {
    case QMetaType::Int: case QMetaType::UInt: case QMetaType::LongLong:
    case QMetaType::ULongLong: case QMetaType::Double: break;
    default: return false;
    }
    const double number = value.toDouble();
    return std::isfinite(number) && number >= static_cast<double>(minimum)
        && number <= static_cast<double>(maximum) && std::floor(number) == number;
}

bool onlyKeys(const QVariantMap &value, const QSet<QString> &allowed)
{
    return std::all_of(value.keyBegin(), value.keyEnd(), [&](const QString &key) { return allowed.contains(key); });
}

bool contentKeys(const QVariantMap &value, bool card)
{
    static const QSet<QString> deckKeys{"id", "name", "description", "createdAt", "parentId"};
    static const QSet<QString> cardKeys{"id", "deckId", "kind", "front", "back", "tags", "pointCount", "narrations", "narrationFingerprints"};
    return onlyKeys(value, card ? cardKeys : deckKeys);
}

std::expected<QVariantMap, QString> parseObject(const QByteArray &bytes)
{
    if (bytes.size() > checkpoints::maxSnapshotBytes)
        return std::unexpected(QStringLiteral("Checkpoint data exceeds the 64 MiB limit."));
    struct QJsonParseError error{};
    const QJsonDocument document = QJsonDocument::fromJson(bytes, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject())
        return std::unexpected(QStringLiteral("Checkpoint data is not a valid JSON object: %1").arg(error.errorString()));
    return document.object().toVariantMap();
}

template<typename Record>
QMap<QString, QVariantMap> recordsById(const QList<Record> &records)
{
    QMap<QString, QVariantMap> result;
    for (const auto &record : records) result.insert(record.id, model::toMap(record));
    return result;
}

QVariantList recordChanges(const QMap<QString, QVariantMap> &before,
                           const QMap<QString, QVariantMap> &after,
                           const QString &labelField)
{
    QSet<QString> ids;
    for (auto it = before.cbegin(); it != before.cend(); ++it) ids.insert(it.key());
    for (auto it = after.cbegin(); it != after.cend(); ++it) ids.insert(it.key());
    QStringList ordered = ids.values();
    std::sort(ordered.begin(), ordered.end());
    QVariantList result;
    for (const QString &id : ordered) {
        const QVariantMap oldRecord = before.value(id);
        const QVariantMap newRecord = after.value(id);
        if (oldRecord == newRecord) continue;
        const QString type = oldRecord.isEmpty() ? QStringLiteral("added")
            : newRecord.isEmpty() ? QStringLiteral("deleted") : QStringLiteral("modified");
        result.append(QVariantMap{{"type", type}, {"id", id},
                                  {labelField, (newRecord.isEmpty() ? oldRecord : newRecord).value(labelField)},
                                  {"before", oldRecord}, {"after", newRecord}});
    }
    return result;
}

std::expected<struct checkpoints::Snapshot, QString> readCurrentSnapshot(QSqlDatabase &database,
                                                                        const QString &rootId)
{
    if (!model::validUuid(rootId))
        return std::unexpected(QStringLiteral("Choose an existing deck before opening checkpoints."));
    QSqlQuery decks(database);
    decks.prepare(QStringLiteral("WITH RECURSIVE subtree(id) AS (SELECT id FROM decks WHERE id=? UNION SELECT d.id FROM decks d JOIN subtree ON d.parent_id=subtree.id) SELECT d.id,d.name,d.description,d.created_at,d.parent_id FROM decks d JOIN subtree s ON s.id=d.id ORDER BY d.id"));
    decks.addBindValue(rootId);
    if (!decks.exec()) return std::unexpected(decks.lastError().text());
    QVariantList deckMaps;
    qsizetype byteCount = 0;
    while (decks.next()) {
        if (deckMaps.size() >= model::maxCollectionRecords)
            return std::unexpected(QStringLiteral("This deck subtree exceeds the checkpoint record limit."));
        const QVariantMap value{{"id", decks.value(0)}, {"name", decks.value(1)},
                                   {"description", decks.value(2)}, {"createdAt", decks.value(3)},
                                   {"parentId", decks.value(4)}};
        byteCount += encoded(value).size() + 1;
        if (byteCount > checkpoints::maxSnapshotBytes)
            return std::unexpected(QStringLiteral("This deck hierarchy exceeds the 64 MiB checkpoint limit. Create checkpoints for smaller subdecks."));
        deckMaps.append(value);
    }
    if (decks.lastError().isValid()) return std::unexpected(decks.lastError().text());
    if (deckMaps.isEmpty())
        return std::unexpected(QStringLiteral("This deck no longer exists. Refresh the library."));
    QSqlQuery cards(database);
    cards.prepare(QStringLiteral("WITH RECURSIVE subtree(id) AS (SELECT id FROM decks WHERE id=? UNION SELECT d.id FROM decks d JOIN subtree ON d.parent_id=subtree.id) SELECT n.id,n.deck_id,n.kind,n.front,n.back,n.tags,n.point_count,n.narrations,n.narration_fingerprints FROM notes n JOIN subtree s ON s.id=n.deck_id ORDER BY n.id"));
    cards.addBindValue(rootId);
    if (!cards.exec()) return std::unexpected(cards.lastError().text());
    QVariantList cardMaps;
    while (cards.next()) {
        if (deckMaps.size() + cardMaps.size() >= model::maxCollectionRecords)
            return std::unexpected(QStringLiteral("This deck subtree exceeds the checkpoint record limit."));
        const auto narrations = parseObject(cards.value(7).toString().toUtf8());
        const auto fingerprints = parseObject(cards.value(8).toString().toUtf8());
        if (!narrations || !fingerprints)
            return std::unexpected(QStringLiteral("A card contains invalid narration metadata. Repair it before creating a checkpoint."));
        const QVariantMap value{{"id", cards.value(0)}, {"deckId", cards.value(1)},
                                {"kind", cards.value(2)}, {"front", cards.value(3)},
                                {"back", cards.value(4)}, {"tags", cards.value(5)},
                                {"pointCount", cards.value(6)}, {"narrations", *narrations},
                                {"narrationFingerprints", *fingerprints}};
        byteCount += encoded(value).size() + 1;
        if (byteCount > checkpoints::maxSnapshotBytes)
            return std::unexpected(QStringLiteral("This deck's content exceeds the 64 MiB checkpoint limit. Create checkpoints for smaller subdecks."));
        cardMaps.append(value);
    }
    if (cards.lastError().isValid()) return std::unexpected(cards.lastError().text());
    return checkpoints::validateSnapshot({{"version", 1}, {"rootId", rootId},
                                           {"decks", deckMaps}, {"cards", cardMaps}});
}

struct StoredCheckpoint {
    QString id{};
    QString deckId{};
    QString createdAt{};
    QString note{};
    QString summary{};
    struct checkpoints::Snapshot snapshot{};
    QVariantMap changes{};
};

std::expected<std::optional<struct StoredCheckpoint>, QString> readCheckpoint(QSqlDatabase &database,
                                                                            const QString &deckId,
                                                                            const QString &id = {})
{
    QSqlQuery query(database);
    query.prepare(id.isEmpty()
        ? QStringLiteral("SELECT id,deck_id,created_at,note,summary,snapshot,changes FROM deck_checkpoints WHERE deck_id=? ORDER BY position DESC LIMIT 1")
        : QStringLiteral("SELECT id,deck_id,created_at,note,summary,snapshot,changes FROM deck_checkpoints WHERE deck_id=? AND id=?"));
    query.addBindValue(deckId);
    if (!id.isEmpty()) query.addBindValue(id);
    if (!query.exec()) return std::unexpected(query.lastError().text());
    if (!query.next()) return std::optional<struct StoredCheckpoint>{};
    const auto snapshotMap = parseObject(query.value(5).toString().toUtf8());
    const auto changesMap = parseObject(query.value(6).toString().toUtf8());
    if (!snapshotMap || !changesMap)
        return std::unexpected(QStringLiteral("This checkpoint contains unreadable content. Preserve the database and choose another checkpoint."));
    const auto snapshot = checkpoints::validateSnapshot(*snapshotMap);
    const auto changes = checkpoints::validateChanges(*changesMap);
    if (!snapshot || !changes || (snapshot && snapshot->rootId != deckId))
        return std::unexpected(QStringLiteral("This checkpoint contains invalid records. Preserve the database and choose another checkpoint."));
    if (!model::validUuid(query.value(0).toString()) || !model::utcInstant(query.value(2).toString())
        || query.value(3).toString().size() > checkpoints::maxNoteLength
        || query.value(4).toString().size() > checkpoints::maxSummaryLength
        || query.value(3).toString().contains(QChar::Null)
        || query.value(4).toString().contains(QChar::Null)
        || changesMap->value("cardCount").toLongLong() != snapshot->cards.size()
        || changesMap->value("deckCount").toLongLong() != snapshot->decks.size())
        return std::unexpected(QStringLiteral("This checkpoint contains invalid metadata. Preserve the database and choose another checkpoint."));
    return std::optional<struct StoredCheckpoint>(StoredCheckpoint{query.value(0).toString(),
        query.value(1).toString(), query.value(2).toString(), query.value(3).toString(),
        query.value(4).toString(), *snapshot, *changesMap});
}

QVariantMap checkpointMetadata(const struct StoredCheckpoint &record)
{
    QVariantMap result{{"id", record.id}, {"deckId", record.deckId}, {"createdAt", record.createdAt},
                       {"note", record.note}, {"summary", record.summary}};
    for (const QString &key : {QStringLiteral("first"), QStringLiteral("added"), QStringLiteral("modified"),
             QStringLiteral("deleted"), QStringLiteral("deckChangeCount"),
             QStringLiteral("cardCount"), QStringLiteral("deckCount")})
        result.insert(key, record.changes.value(key));
    return result;
}

struct checkpoints::Snapshot restoredAtCurrentParent(const struct checkpoints::Snapshot &current,
                                                     const struct checkpoints::Snapshot &target)
{
    assert(current.rootId == target.rootId);
    const auto root = std::find_if(current.decks.cbegin(), current.decks.cend(), [&](const struct model::Deck &entry) { return entry.id == current.rootId; });
    assert(root != current.decks.cend());
    struct checkpoints::Snapshot result = target;
    for (struct model::Deck &record : result.decks)
        if (record.id == result.rootId) record.parentId = root->parentId;
    return result;
}

QHash<QString, int> hierarchyDepths(const QList<struct model::Deck> &decks)
{
    QHash<QString, QString> parents;
    for (const struct model::Deck &record : decks) parents.insert(record.id, record.parentId);
    QHash<QString, int> result;
    for (const struct model::Deck &record : decks) {
        QString current = record.id;
        QStringList path;
        while (parents.contains(current) && !result.contains(current)) {
            path.append(current);
            assert(path.size() <= decks.size());
            current = parents.value(current);
        }
        int depth = result.value(current);
        for (auto it = path.crbegin(); it != path.crend(); ++it) result.insert(*it, ++depth);
    }
    return result;
}

std::expected<QList<struct model::Deck>, QString> readAncestors(QSqlDatabase &database,
                                                               const struct checkpoints::Snapshot &snapshot)
{
    const auto root = std::find_if(snapshot.decks.cbegin(), snapshot.decks.cend(), [&](const struct model::Deck &entry) { return entry.id == snapshot.rootId; });
    assert(root != snapshot.decks.cend());
    QList<struct model::Deck> result;
    QSet<QString> seen{snapshot.rootId};
    QString parent = root->parentId;
    qsizetype byteCount = 0;
    while (!parent.isEmpty()) {
        if (seen.contains(parent) || result.size() >= model::maxCollectionRecords)
            return std::unexpected(QStringLiteral("The deck's ancestor chain is cyclic or exceeds its record limit. Repair the hierarchy before restoring."));
        seen.insert(parent);
        QSqlQuery query(database);
        query.prepare("SELECT id,name,description,created_at,parent_id FROM decks WHERE id=?");
        query.addBindValue(parent);
        if (!query.exec()) return std::unexpected(query.lastError().text());
        if (!query.next()) return std::unexpected(QStringLiteral("A parent deck is missing. Move this deck to an existing parent before restoring."));
        const QVariantMap value{{"id", query.value(0)}, {"name", query.value(1)},
                                {"description", query.value(2)}, {"createdAt", query.value(3)},
                                {"parentId", query.value(4)}};
        const auto record = model::deckFromMap(value);
        if (!record) return std::unexpected(record.error());
        byteCount += encoded(value).size();
        if (byteCount > checkpoints::maxSnapshotBytes)
            return std::unexpected(QStringLiteral("The deck's ancestor metadata exceeds the 64 MiB restore limit."));
        result.append(*record);
        parent = record->parentId;
    }
    std::reverse(result.begin(), result.end());
    return result;
}

QString contentToken(const struct checkpoints::Snapshot &current,
                     const std::optional<struct StoredCheckpoint> &target,
                     const QString &action)
{
    const QVariantMap value{{"action", action}, {"current", checkpoints::snapshotMap(current)},
                            {"checkpointId", target ? target->id : QString()},
                            {"checkpoint", target ? checkpoints::snapshotMap(target->snapshot) : QVariantMap{}}};
    return QString::fromLatin1(QCryptographicHash::hash(encoded(value), QCryptographicHash::Sha256).toHex());
}

QString mediaDirectory(const QString &databasePath)
{
    return QDir(QFileInfo(databasePath).absolutePath()).filePath(QStringLiteral("media"));
}

std::expected<QList<struct model::MediaAttachment>, QString> checkpointMedia(QSqlDatabase &database,
                                                                           const struct checkpoints::Snapshot &snapshot,
                                                                           const QString &directory,
                                                                           bool preferCache)
{
    const auto names = model::mediaReferences(snapshot.cards);
    if (!names) return std::unexpected(names.error());
    if (!preferCache) return model::readBackupMedia(directory, *names);
    QList<struct model::MediaAttachment> result;
    qint64 byteCount = 0;
    for (const QString &name : *names) {
        QSqlQuery query(database);
        query.prepare(QStringLiteral("SELECT bytes FROM checkpoint_media WHERE name=?"));
        query.addBindValue(name);
        if (!query.exec()) return std::unexpected(query.lastError().text());
        if (!query.next()) {
            const auto live = model::readBackupMedia(directory, {name});
            if (!live) return std::unexpected(live.error());
            assert(live->size() == 1);
            byteCount += live->first().bytes.size();
            result.append(live->first());
        } else {
            const QByteArray bytes = query.value(0).toByteArray();
            const auto valid = model::validateMediaAttachment(name, bytes);
            if (!valid) return std::unexpected(valid.error());
            byteCount += bytes.size();
            result.append(model::MediaAttachment{name, bytes});
        }
        if (byteCount > model::maxMediaBytes)
            return std::unexpected(QStringLiteral("Checkpoint media exceeds the 64 MiB limit."));
    }
    return result;
}
}

namespace betterflash::checkpoints {
std::expected<struct Snapshot, QString> validateSnapshot(const QVariantMap &value)
{
    if (!onlyKeys(value, {"version", "rootId", "decks", "cards"})
        || !integer(value.value("version"), 1, 1)
        || value.value("rootId").metaType().id() != QMetaType::QString
        || !model::validUuid(value.value("rootId").toString())
        || !isList(value.value("decks")) || !isList(value.value("cards")))
        return std::unexpected(QStringLiteral("Checkpoint snapshot fields are invalid."));
    const QVariantList decks = value.value("decks").toList();
    const QVariantList cards = value.value("cards").toList();
    if (decks.isEmpty() || decks.size() + cards.size() > model::maxCollectionRecords
        || encoded(value).size() > maxSnapshotBytes)
        return std::unexpected(QStringLiteral("Checkpoint snapshot is empty or exceeds its record or size limit."));
    struct Snapshot result{value.value("rootId").toString(), {}, {}};
    QHash<QString, QString> parents;
    for (const QVariant &entry : decks) {
        if (!isMap(entry) || !contentKeys(entry.toMap(), false)) return std::unexpected(QStringLiteral("Every checkpoint deck must contain only deck content fields."));
        const auto deck = model::deckFromMap(entry.toMap());
        if (!deck) return std::unexpected(deck.error());
        if (parents.contains(deck->id)) return std::unexpected(QStringLiteral("Checkpoint deck identifiers are duplicated."));
        parents.insert(deck->id, deck->parentId);
        result.decks.append(*deck);
    }
    if (!parents.contains(result.rootId) || parents.contains(parents.value(result.rootId)))
        return std::unexpected(QStringLiteral("Checkpoint root is missing or its parent is inside its own subtree."));
    QSet<QString> reachesRoot{result.rootId};
    for (auto it = parents.cbegin(); it != parents.cend(); ++it) {
        QSet<QString> seen;
        QString current = it.key();
        while (!reachesRoot.contains(current)) {
            if (!parents.contains(current) || seen.contains(current))
                return std::unexpected(QStringLiteral("Checkpoint decks contain a cycle or do not descend from the selected deck."));
            seen.insert(current);
            current = parents.value(current);
        }
        reachesRoot.unite(seen);
    }
    QSet<QString> ids;
    for (const QVariant &entry : cards) {
        if (!isMap(entry) || !contentKeys(entry.toMap(), true)) return std::unexpected(QStringLiteral("Every checkpoint card must contain only card content fields."));
        const auto card = model::cardFromMap(entry.toMap());
        if (!card) return std::unexpected(card.error());
        if (!parents.contains(card->deckId) || ids.contains(card->id))
            return std::unexpected(QStringLiteral("Checkpoint card identifiers or deck references are invalid."));
        ids.insert(card->id);
        result.cards.append(*card);
    }
    std::sort(result.decks.begin(), result.decks.end(), [](const struct model::Deck &a, const struct model::Deck &b) { return a.id < b.id; });
    std::sort(result.cards.begin(), result.cards.end(), [](const struct model::Card &a, const struct model::Card &b) { return a.id < b.id; });
    return result;
}

QVariantMap snapshotMap(const struct Snapshot &snapshot)
{
    QVariantList decks;
    QVariantList cards;
    for (const struct model::Deck &deck : snapshot.decks) decks.append(model::toMap(deck));
    for (const struct model::Card &card : snapshot.cards) cards.append(model::toMap(card));
    return {{"version", 1}, {"rootId", snapshot.rootId}, {"decks", decks}, {"cards", cards}};
}

QVariantMap diff(const struct Snapshot &before, const struct Snapshot &after, bool first)
{
    const QVariantList cards = recordChanges(recordsById(before.cards), recordsById(after.cards), QStringLiteral("front"));
    const QVariantList decks = recordChanges(recordsById(before.decks), recordsById(after.decks), QStringLiteral("name"));
    int added = 0;
    int modified = 0;
    int deleted = 0;
    for (const QVariant &value : cards) {
        const QString type = value.toMap().value("type").toString();
        if (type == QStringLiteral("added")) ++added;
        else if (type == QStringLiteral("modified")) ++modified;
        else ++deleted;
    }
    return {{"first", first}, {"added", added}, {"modified", modified}, {"deleted", deleted},
            {"deckChangeCount", decks.size()}, {"cardCount", after.cards.size()},
            {"deckCount", after.decks.size()}, {"changes", cards}, {"deckChanges", decks}};
}

std::expected<void, QString> validateChanges(const QVariantMap &value)
{
    if (!onlyKeys(value, {"first", "added", "modified", "deleted", "deckChangeCount", "cardCount", "deckCount", "changes", "deckChanges"})
        || !isList(value.value("changes")) || !isList(value.value("deckChanges"))
        || value.value("first").metaType().id() != QMetaType::Bool
        || encoded(value).size() > maxSnapshotBytes)
        return std::unexpected(QStringLiteral("Checkpoint change information is invalid or exceeds its size limit."));
    QMap<QString, int> counts{{"added", 0}, {"modified", 0}, {"deleted", 0}};
    for (const QString &listKey : {QStringLiteral("changes"), QStringLiteral("deckChanges")}) {
        const QVariantList entries = value.value(listKey).toList();
        if (entries.size() > model::maxCollectionRecords * 2)
            return std::unexpected(QStringLiteral("Checkpoint change information exceeds its record limit."));
        QSet<QString> ids;
        for (const QVariant &entry : entries) {
            if (!isMap(entry)) return std::unexpected(QStringLiteral("Every checkpoint change must be an object."));
            const QVariantMap change = entry.toMap();
            const QString id = change.value("id").toString();
            const QString type = change.value("type").toString();
            const QSet<QString> allowed = listKey == QStringLiteral("changes")
                ? QSet<QString>{"type", "id", "front", "before", "after"}
                : QSet<QString>{"type", "id", "name", "before", "after"};
            if (!onlyKeys(change, allowed)
                || change.value("id").metaType().id() != QMetaType::QString
                || change.value("type").metaType().id() != QMetaType::QString
                || !model::validUuid(id) || ids.contains(id) || !counts.contains(type)
                || !isMap(change.value("before")) || !isMap(change.value("after")))
                return std::unexpected(QStringLiteral("Checkpoint change fields are invalid."));
            ids.insert(id);
            const QVariantMap before = change.value("before").toMap();
            const QVariantMap after = change.value("after").toMap();
            if ((type == QStringLiteral("added") && (!before.isEmpty() || after.isEmpty()))
                || (type == QStringLiteral("deleted") && (before.isEmpty() || !after.isEmpty()))
                || (type == QStringLiteral("modified") && (before.isEmpty() || after.isEmpty() || before == after)))
                return std::unexpected(QStringLiteral("Checkpoint change type does not match its contents."));
            for (const QVariantMap &record : {before, after}) {
                if (record.isEmpty()) continue;
                if (record.value("id").toString() != id)
                    return std::unexpected(QStringLiteral("Checkpoint change identifier does not match its record."));
                if (listKey == QStringLiteral("changes")) {
                    if (!contentKeys(record, true)) return std::unexpected(QStringLiteral("Checkpoint card changes contain fields outside card content."));
                    const auto valid = model::cardFromMap(record);
                    if (!valid) return std::unexpected(valid.error());
                    if (model::toMap(*valid) != record)
                        return std::unexpected(QStringLiteral("Checkpoint card changes must contain complete normalized content fields."));
                } else {
                    if (!contentKeys(record, false)) return std::unexpected(QStringLiteral("Checkpoint deck changes contain fields outside deck content."));
                    const auto valid = model::deckFromMap(record);
                    if (!valid) return std::unexpected(valid.error());
                    if (model::toMap(*valid) != record)
                        return std::unexpected(QStringLiteral("Checkpoint deck changes must contain complete normalized content fields."));
                }
            }
            const QString label = listKey == QStringLiteral("changes") ? QStringLiteral("front") : QStringLiteral("name");
            if (change.value(label).metaType().id() != QMetaType::QString
                || change.value(label) != (after.isEmpty() ? before : after).value(label))
                return std::unexpected(QStringLiteral("Checkpoint change label does not match its content."));
            if (value.value("first").toBool() && type != QStringLiteral("added"))
                return std::unexpected(QStringLiteral("A first checkpoint can only contain added records."));
            if (listKey == QStringLiteral("changes")) ++counts[type];
        }
    }
    for (const QString &key : {QStringLiteral("added"), QStringLiteral("modified"), QStringLiteral("deleted"),
                              QStringLiteral("deckChangeCount"), QStringLiteral("cardCount"), QStringLiteral("deckCount")}) {
        const QVariant number = value.value(key);
        if (!integer(number, 0, model::maxCollectionRecords * 2))
            return std::unexpected(QStringLiteral("Checkpoint change counts are invalid."));
        const qlonglong count = number.toLongLong();
        if (counts.contains(key) && count != counts.value(key))
            return std::unexpected(QStringLiteral("Checkpoint change counts do not match their records."));
    }
    if (value.value("deckChangeCount").toLongLong() != value.value("deckChanges").toList().size())
        return std::unexpected(QStringLiteral("Checkpoint deck change count does not match its records."));
    if (value.value("first").toBool()
        && (value.value("added").toLongLong() != value.value("cardCount").toLongLong()
            || value.value("deckChangeCount").toLongLong() != value.value("deckCount").toLongLong()))
        return std::unexpected(QStringLiteral("A first checkpoint must describe every deck and card as added."));
    return {};
}
}

void DatabaseWorker::loadDeckCheckpoints(const QString &deckId, const QString &requestId)
{
    const auto reject = [&](const QString &message) {
        emit deckCheckpointsReady(deckId, {{"ok", false}, {"error", message}}, requestId);
    };
    assertThread();
    m_sqlDetail.clear();
    if (!m_db.isOpen()) { reject(QStringLiteral("Storage is unavailable. Restart the application and try again.")); return; }
    const auto current = readCurrentSnapshot(m_db, deckId);
    if (!current) { reject(current.error()); return; }
    const auto latest = readCheckpoint(m_db, deckId);
    if (!latest) { reject(latest.error()); return; }
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("SELECT id,created_at,note,summary,json_extract(changes,'$.first'),json_extract(changes,'$.added'),json_extract(changes,'$.modified'),json_extract(changes,'$.deleted'),json_extract(changes,'$.deckChangeCount'),json_extract(changes,'$.cardCount'),json_extract(changes,'$.deckCount') FROM deck_checkpoints WHERE deck_id=? ORDER BY position"));
    query.addBindValue(deckId);
    if (!query.exec()) { reject(query.lastError().text()); return; }
    QVariantList records;
    while (query.next()) {
        if (records.size() >= model::maxCollectionRecords) {
            reject(QStringLiteral("This deck has too many checkpoints to display. Preserve the database and inspect the history.")); return;
        }
        QVariantMap metadata{{"id", query.value(0)}, {"deckId", deckId}, {"createdAt", query.value(1)},
                             {"note", query.value(2)}, {"summary", query.value(3)}};
        if (!model::validUuid(query.value(0).toString()) || !model::utcInstant(query.value(1).toString())
            || query.value(2).toString().size() > checkpoints::maxNoteLength
            || query.value(3).toString().size() > checkpoints::maxSummaryLength
            || query.value(2).toString().contains(QChar::Null) || query.value(3).toString().contains(QChar::Null)) {
            reject(QStringLiteral("Checkpoint history contains invalid annotations. Preserve the database and inspect the history.")); return;
        }
        int column = 4;
        for (const QString &key : {QStringLiteral("first"), QStringLiteral("added"), QStringLiteral("modified"),
                 QStringLiteral("deleted"), QStringLiteral("deckChangeCount"), QStringLiteral("cardCount"), QStringLiteral("deckCount")}) {
            const QVariant value = query.value(column++);
            if (!integer(value, 0, key == QStringLiteral("first") ? 1 : model::maxCollectionRecords * 2)) {
                reject(QStringLiteral("Checkpoint history contains invalid summary counts. Preserve the database and inspect the history.")); return;
            }
            metadata.insert(key, key == QStringLiteral("first") ? QVariant(value.toBool()) : value);
        }
        records.append(metadata);
    }
    if (query.lastError().isValid()) { reject(query.lastError().text()); return; }
    QVariantMap preview = checkpoints::diff(*latest ? (*latest)->snapshot : checkpoints::Snapshot{}, *current, !*latest);
    preview.insert("token", contentToken(*current, *latest, QStringLiteral("create")));
    emit deckCheckpointsReady(deckId, {{"ok", true}, {"checkpoints", records}, {"preview", preview}}, requestId);
}

void DatabaseWorker::loadCheckpointChanges(const QString &deckId, const QString &checkpointId, const QString &requestId)
{
    const auto reject = [&](const QString &message) {
        emit checkpointChangesReady(deckId, checkpointId, {{"ok", false}, {"error", message}}, requestId);
    };
    assertThread();
    m_sqlDetail.clear();
    if (!m_db.isOpen()) { reject(QStringLiteral("Storage is unavailable. Restart the application and try again.")); return; }
    if (!model::validUuid(checkpointId)) { reject(QStringLiteral("Select a valid checkpoint.")); return; }
    const auto current = readCurrentSnapshot(m_db, deckId);
    const auto target = readCheckpoint(m_db, deckId, checkpointId);
    if (!current) { reject(current.error()); return; }
    if (!target) { reject(target.error()); return; }
    if (!*target) { reject(QStringLiteral("This checkpoint no longer exists. Refresh the checkpoint timeline.")); return; }
    const struct checkpoints::Snapshot restored = restoredAtCurrentParent(*current, (*target)->snapshot);
    QVariantMap result = checkpoints::diff(*current, restored);
    result.insert("ok", true);
    result.insert("token", contentToken(*current, *target, QStringLiteral("restore")));
    QVariantMap metadata = checkpointMetadata(**target);
    metadata.insert("historicalChanges", (*target)->changes);
    result.insert("checkpoint", metadata);
    emit checkpointChangesReady(deckId, checkpointId, result, requestId);
}

void DatabaseWorker::createDeckCheckpoint(const QString &deckId, const QString &note, const QString &summary,
                                         const QString &expectedToken, const QString &requestId)
{
    const auto reject = [&](const QString &code, const QString &message, const QString &detail = QString()) {
        fail(code, message, detail);
        emit deckCheckpointFinished(deckId, {}, QStringLiteral("create"), false, requestId);
    };
    if (!ready()) { emit deckCheckpointFinished(deckId, {}, QStringLiteral("create"), false, requestId); return; }
    if (note.size() > checkpoints::maxNoteLength || summary.size() > checkpoints::maxSummaryLength
        || note.contains(QChar::Null) || summary.contains(QChar::Null)) {
        reject("CHECKPOINT_NOTE_LIMIT", "Shorten the checkpoint note to 4096 characters and the summary to 8192 characters."); return;
    }
    const auto current = readCurrentSnapshot(m_db, deckId);
    const auto latest = readCheckpoint(m_db, deckId);
    if (!current) { reject("CHECKPOINT_DECK_INVALID", "Could not read the deck. Refresh the library and try again.", current.error()); return; }
    if (!latest) { reject("CHECKPOINT_HISTORY_INVALID", "Could not read checkpoint history. Preserve the database and inspect the details.", latest.error()); return; }
    if (expectedToken.isEmpty() || expectedToken != contentToken(*current, *latest, QStringLiteral("create"))) {
        reject("CHECKPOINT_STALE", "The deck or its checkpoint history changed. Refresh the changes and create the checkpoint again."); return;
    }
    const auto media = checkpointMedia(m_db, *current, mediaDirectory(m_path), false);
    if (!media) { reject("CHECKPOINT_MEDIA", "Could not preserve checkpoint media. Restore missing assets and try again.", media.error()); return; }
    const QVariantMap changes = checkpoints::diff(*latest ? (*latest)->snapshot : checkpoints::Snapshot{}, *current, !*latest);
    const QByteArray snapshotBytes = encoded(checkpoints::snapshotMap(*current));
    const QByteArray changeBytes = encoded(changes);
    if (changeBytes.size() > checkpoints::maxSnapshotBytes) {
        reject("CHECKPOINT_TOO_LARGE", "The change list exceeds 64 MiB. Create checkpoints for smaller subdecks."); return;
    }
    const QString checkpointId = model::uuid();
    if (!mutate([&] {
        const auto checkedCurrent = readCurrentSnapshot(m_db, deckId);
        const auto checkedLatest = readCheckpoint(m_db, deckId);
        if (!checkedCurrent || !checkedLatest) {
            m_sqlDetail = !checkedCurrent ? checkedCurrent.error() : checkedLatest.error(); return false;
        }
        if (expectedToken != contentToken(*checkedCurrent, *checkedLatest, QStringLiteral("create"))) {
            m_sqlDetail = QStringLiteral("The deck changed before the checkpoint could be saved. Refresh the changes and try again."); return false;
        }
        for (const struct model::MediaAttachment &asset : *media)
            if (!execute("INSERT INTO checkpoint_media(name,bytes) VALUES(?,?) ON CONFLICT(name) DO UPDATE SET bytes=excluded.bytes", {asset.name, asset.bytes})) return false;
        return execute("INSERT INTO deck_checkpoints(id,deck_id,created_at,note,summary,snapshot,changes) VALUES(?,?,?,?,?,?,?)",
                       {checkpointId, deckId, model::nowUtc(), note.trimmed(), summary.trimmed(),
                        QString::fromUtf8(snapshotBytes), QString::fromUtf8(changeBytes)});
    })) { reject("CHECKPOINT_SAVE", "Could not save the checkpoint. No deck content changed. Check storage and try again.", m_sqlDetail); return; }
    done("Deck checkpoint saved");
    emit deckCheckpointFinished(deckId, checkpointId, QStringLiteral("create"), true, requestId);
}

void DatabaseWorker::restoreDeckCheckpoint(const QString &deckId, const QString &checkpointId,
                                          const QString &expectedToken, const QString &requestId)
{
    const auto reject = [&](const QString &code, const QString &message, const QString &detail = QString()) {
        fail(code, message, detail);
        emit deckCheckpointFinished(deckId, checkpointId, QStringLiteral("restore"), false, requestId);
    };
    if (!ready()) { emit deckCheckpointFinished(deckId, checkpointId, QStringLiteral("restore"), false, requestId); return; }
    if (m_sessionActive || !m_sessionOrder.isEmpty()) {
        reject("CHECKPOINT_REVIEW_ACTIVE", "End the current review before restoring a deck checkpoint."); return;
    }
    if (!model::validUuid(checkpointId)) { reject("CHECKPOINT_INVALID", "Select an existing checkpoint."); return; }
    const auto current = readCurrentSnapshot(m_db, deckId);
    const auto target = readCheckpoint(m_db, deckId, checkpointId);
    if (!current) { reject("CHECKPOINT_DECK_INVALID", "Could not read the current deck. Refresh the library and try again.", current.error()); return; }
    if (!target) { reject("CHECKPOINT_HISTORY_INVALID", "Could not read the checkpoint. Preserve the database and choose another checkpoint.", target.error()); return; }
    if (!*target) { reject("CHECKPOINT_NOT_FOUND", "This checkpoint no longer exists. Refresh the checkpoint timeline."); return; }
    if (expectedToken.isEmpty() || expectedToken != contentToken(*current, *target, QStringLiteral("restore"))) {
        reject("CHECKPOINT_STALE", "The deck or selected checkpoint changed. Refresh the restore preview and try again."); return;
    }
    const struct checkpoints::Snapshot restored = restoredAtCurrentParent(*current, (*target)->snapshot);
    QSet<QString> currentDeckIds;
    QSet<QString> currentCardIds;
    for (const struct model::Deck &record : current->decks) currentDeckIds.insert(record.id);
    for (const struct model::Card &record : current->cards) currentCardIds.insert(record.id);
    QSet<QString> restoredDeckIds;
    QSet<QString> restoredCardIds;
    for (const struct model::Deck &record : restored.decks) {
        restoredDeckIds.insert(record.id);
        const auto existing = deck(record.id);
        if (!m_sqlDetail.isEmpty()) { reject("CHECKPOINT_STORAGE", "Could not inspect current decks. Check storage and try again.", m_sqlDetail); return; }
        if (existing && !currentDeckIds.contains(record.id)) {
            reject("CHECKPOINT_EXTERNAL_CONFLICT", "A checkpoint subdeck now lives outside this deck. Move it back or restore a different checkpoint.", QStringLiteral("Subdeck: %1 (%2)").arg(existing->name, existing->id)); return;
        }
    }
    for (const struct model::Card &record : restored.cards) {
        restoredCardIds.insert(record.id);
        const auto existing = card(record.id);
        if (!m_sqlDetail.isEmpty()) { reject("CHECKPOINT_STORAGE", "Could not inspect current cards. Check storage and try again.", m_sqlDetail); return; }
        if (existing && !currentCardIds.contains(record.id)) {
            reject("CHECKPOINT_EXTERNAL_CONFLICT", "A checkpoint card now lives outside this deck. Move it back or restore a different checkpoint.", QStringLiteral("Card: %1").arg(existing->id)); return;
        }
    }
    const auto restoredValid = checkpoints::validateSnapshot(checkpoints::snapshotMap(restored));
    if (!restoredValid) { reject("CHECKPOINT_HIERARCHY", "The restored hierarchy would be invalid. Move this deck to a valid parent and try again.", restoredValid.error()); return; }
    const auto ancestors = readAncestors(m_db, *current);
    if (!ancestors) { reject("CHECKPOINT_HIERARCHY", "Could not inspect the parent folders required by this deck. Repair the hierarchy and try again.", ancestors.error()); return; }
    const auto targetMedia = checkpointMedia(m_db, restored, mediaDirectory(m_path), true);
    const auto recoveryMedia = checkpointMedia(m_db, *current, mediaDirectory(m_path), false);
    if (!targetMedia || !recoveryMedia) {
        reject("CHECKPOINT_MEDIA", "Could not verify checkpoint or recovery media. Restore missing assets and try again.", !targetMedia ? targetMedia.error() : recoveryMedia.error()); return;
    }
    const auto materialized = model::writeBackupMedia(mediaDirectory(m_path), *targetMedia);
    if (!materialized) { reject("CHECKPOINT_MEDIA", "Could not restore checkpoint media. No deck records changed. Check media storage and try again.", materialized.error()); return; }
    for (const struct model::Card &record : restored.cards)
        if (!validateMedia(record)) { reject("CHECKPOINT_MEDIA", "Restored media did not pass validation. No deck records changed.", m_sqlDetail); return; }
    const auto latest = readCheckpoint(m_db, deckId);
    if (!latest) { reject("CHECKPOINT_HISTORY_INVALID", "Could not preserve the current deck before restoring. Check checkpoint history.", latest.error()); return; }
    const QVariantMap recoveryChanges = checkpoints::diff(*latest ? (*latest)->snapshot : checkpoints::Snapshot{}, *current, !*latest);
    if (encoded(recoveryChanges).size() > checkpoints::maxSnapshotBytes) {
        reject("CHECKPOINT_TOO_LARGE", "The recovery checkpoint change list exceeds 64 MiB. Create checkpoints for smaller subdecks."); return;
    }
    const QString recoveryId = model::uuid();
    const QString now = model::nowUtc();
    const QHash<QString, int> restoredDepths = hierarchyDepths(restored.decks);
    QList<struct model::Deck> deckOrder = restored.decks;
    std::sort(deckOrder.begin(), deckOrder.end(), [&](const struct model::Deck &a, const struct model::Deck &b) { const int left = restoredDepths.value(a.id); const int right = restoredDepths.value(b.id); return left == right ? a.id < b.id : left < right; });
    if (!mutate([&] {
        const auto checkedCurrent = readCurrentSnapshot(m_db, deckId);
        const auto checkedTarget = readCheckpoint(m_db, deckId, checkpointId);
        const auto checkedLatest = readCheckpoint(m_db, deckId);
        if (!checkedCurrent || !checkedTarget || !checkedLatest) {
            m_sqlDetail = !checkedCurrent ? checkedCurrent.error() : !checkedTarget ? checkedTarget.error() : checkedLatest.error(); return false;
        }
        if (!*checkedTarget || expectedToken != contentToken(*checkedCurrent, *checkedTarget, QStringLiteral("restore"))
            || (static_cast<bool>(*checkedLatest) != static_cast<bool>(*latest))
            || (*latest && (*checkedLatest)->id != (*latest)->id)) {
            m_sqlDetail = QStringLiteral("The deck or checkpoint history changed before restoration. Refresh the restore preview and try again."); return false;
        }
        const auto checkedAncestors = readAncestors(m_db, *checkedCurrent);
        if (!checkedAncestors) { m_sqlDetail = checkedAncestors.error(); return false; }
        if (recordsById(*checkedAncestors) != recordsById(*ancestors)) {
            m_sqlDetail = QStringLiteral("A parent deck changed before restoration. Refresh the restore preview and try again."); return false;
        }
        for (const struct model::MediaAttachment &asset : *recoveryMedia)
            if (!execute("INSERT INTO checkpoint_media(name,bytes) VALUES(?,?) ON CONFLICT(name) DO UPDATE SET bytes=excluded.bytes", {asset.name, asset.bytes})) return false;
        for (const struct model::MediaAttachment &asset : *targetMedia)
            if (!execute("INSERT INTO checkpoint_media(name,bytes) VALUES(?,?) ON CONFLICT(name) DO UPDATE SET bytes=excluded.bytes", {asset.name, asset.bytes})) return false;
        if (!execute("INSERT INTO deck_checkpoints(id,deck_id,created_at,note,summary,snapshot,changes) VALUES(?,?,?,?,?,?,?)",
                     {recoveryId, deckId, now, QStringLiteral("Before restoring checkpoint %1").arg(checkpointId), QStringLiteral("Automatic recovery checkpoint preserving the deck before restoration."), QString::fromUtf8(encoded(checkpoints::snapshotMap(*current))), QString::fromUtf8(encoded(recoveryChanges))})) return false;
        for (const struct model::Deck &record : *ancestors)
            if (!enqueue("deck.upsert", {{"deck", model::toMap(record)}, {"restore", true}, {"restoreDependency", true}})) return false;
        for (const struct model::Deck &record : deckOrder) {
            const auto existing = deck(record.id);
            if (!m_sqlDetail.isEmpty()) return false;
            if (existing && !currentDeckIds.contains(record.id)) {
                m_sqlDetail = QStringLiteral("A checkpoint subdeck moved outside this deck before restoration. Refresh the preview and move it back before restoring."); return false;
            }
            if (!execute("DELETE FROM tombstones WHERE entity_type='deck' AND entity_id=?", {record.id})) return false;
            const QVariantMap payload{{"deck", model::toMap(record)}, {"restore", true}};
            if (!upsertDeck(record) || !enqueue("deck.upsert", payload)) return false;
        }
        for (const struct model::Card &record : restored.cards) {
            const auto existing = card(record.id);
            if (!m_sqlDetail.isEmpty()) return false;
            if (existing && !currentCardIds.contains(record.id)) {
                m_sqlDetail = QStringLiteral("A checkpoint card moved outside this deck before restoration. Refresh the preview and move it back before restoring."); return false;
            }
            if (!execute("DELETE FROM tombstones WHERE entity_type='card' AND entity_id=?", {record.id})) return false;
            QList<struct model::Variant> schedules;
            QVariantList freshVariantIds;
            const QList<struct model::Variant> retained = cardVariants(record.id);
            if (!m_sqlDetail.isEmpty()) return false;
            const auto keys = model::variantKeys(record);
            assert(keys.has_value());
            for (const QString &key : *keys) {
                const QString variantId = model::variantId(record.id, key);
                const auto found = std::find_if(retained.cbegin(), retained.cend(), [&](const struct model::Variant &entry) { return entry.id == variantId; });
                schedules.append(found == retained.cend() ? model::Variant{variantId, record.id, key, now} : *found);
                if (found == retained.cend()) freshVariantIds.append(variantId);
            }
            if (!upsertCard(record) || !replaceVariants(record, schedules)) return false;
            QVariantMap payload = cardPayload(record);
            payload.insert("restore", true);
            payload.insert("freshVariantIds", freshVariantIds);
            if (!enqueue("card.upsert", payload)) return false;
        }
        for (const struct model::Card &record : current->cards)
            if (!restoredCardIds.contains(record.id)
                && (!eraseCard(record.id) || !enqueue("card.delete", {{"id", record.id}}))) return false;
        const QHash<QString, int> currentDepths = hierarchyDepths(current->decks);
        QStringList removedDecks;
        for (const struct model::Deck &record : current->decks)
            if (!restoredDeckIds.contains(record.id)) removedDecks.append(record.id);
        std::sort(removedDecks.begin(), removedDecks.end(), [&](const QString &a, const QString &b) { const int left = currentDepths.value(a); const int right = currentDepths.value(b); return left == right ? a < b : left > right; });
        for (const QString &id : removedDecks)
            if (!tombstone("deck", id) || !execute("DELETE FROM decks WHERE id=?", {id})
                || !enqueue("deck.delete", {{"id", id}})) return false;
        return true;
    })) { reject("CHECKPOINT_RESTORE", "Could not restore the checkpoint. Deck records, review state, and pending sync changes were preserved. Check storage and try again.", m_sqlDetail); return; }
    m_queueDirty = true;
    if (publishSnapshot() && publishQueue(false)) done("Deck checkpoint restored; the previous content was saved as a recovery checkpoint");
    emit deckCheckpointFinished(deckId, checkpointId, QStringLiteral("restore"), true, requestId);
}

void DatabaseWorker::deleteDeckCheckpoint(const QString &deckId, const QString &checkpointId, const QString &requestId)
{
    const auto reject = [&](const QString &code, const QString &message, const QString &detail = QString()) {
        fail(code, message, detail);
        emit deckCheckpointFinished(deckId, checkpointId, QStringLiteral("delete"), false, requestId);
    };
    if (!ready()) { emit deckCheckpointFinished(deckId, checkpointId, QStringLiteral("delete"), false, requestId); return; }
    if (!model::validUuid(deckId) || !model::validUuid(checkpointId)) {
        reject("CHECKPOINT_INVALID", "Select an existing deck and checkpoint."); return;
    }
    if (!deck(deckId)) {
        reject("CHECKPOINT_DECK_INVALID", "This deck no longer exists. Refresh the library.", m_sqlDetail); return;
    }
    const auto target = readCheckpoint(m_db, deckId, checkpointId);
    if (!target) { reject("CHECKPOINT_HISTORY_INVALID", "Could not inspect the checkpoint. Preserve the database and inspect the details.", target.error()); return; }
    if (!*target) { reject("CHECKPOINT_NOT_FOUND", "This checkpoint no longer exists. Refresh the checkpoint timeline."); return; }
    if (!mutate([&] {
        if (!execute("DELETE FROM deck_checkpoints WHERE deck_id=? AND id=?", {deckId, checkpointId})) return false;
        QSet<QString> retainedMedia;
        QSqlQuery snapshots(m_db);
        snapshots.setForwardOnly(true);
        if (!snapshots.exec("SELECT snapshot FROM deck_checkpoints ORDER BY position")) {
            m_sqlDetail = snapshots.lastError().text(); return false;
        }
        while (snapshots.next()) {
            const auto value = parseObject(snapshots.value(0).toString().toUtf8());
            if (!value) { m_sqlDetail = value.error(); return false; }
            const auto snapshot = checkpoints::validateSnapshot(*value);
            if (!snapshot) { m_sqlDetail = snapshot.error(); return false; }
            const auto names = model::mediaReferences(snapshot->cards);
            if (!names) { m_sqlDetail = names.error(); return false; }
            for (const QString &name : *names) retainedMedia.insert(name);
        }
        if (snapshots.lastError().isValid()) { m_sqlDetail = snapshots.lastError().text(); return false; }
        snapshots.finish();
        QSqlQuery assets(m_db);
        if (!assets.exec("SELECT name FROM checkpoint_media")) { m_sqlDetail = assets.lastError().text(); return false; }
        QStringList unused;
        while (assets.next()) {
            const QString name = assets.value(0).toString();
            if (!retainedMedia.contains(name)) unused.append(name);
        }
        if (assets.lastError().isValid()) { m_sqlDetail = assets.lastError().text(); return false; }
        assets.finish();
        for (const QString &name : unused)
            if (!execute("DELETE FROM checkpoint_media WHERE name=?", {name})) return false;
        return true;
    })) { reject("CHECKPOINT_DELETE", "Could not delete the checkpoint. Checkpoint history and media were preserved. Check storage and inspect the details.", m_sqlDetail); return; }
    done("Checkpoint deleted; deck content and review history were preserved");
    emit deckCheckpointFinished(deckId, checkpointId, QStringLiteral("delete"), true, requestId);
}
