#include "databaseworker.h"
#include "scheduler.h"
#include "mediabackup.h"
#include "syncprotocol.h"

#include <QDate>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QSet>
#include <QSqlError>
#include <QSqlQuery>
#include <QThread>
#include <QTimeZone>
#include <algorithm>
#include <cassert>
#include <cmath>

namespace model = betterflash::model;
namespace {
QVariantMap object(const QVariantMap &map, const QString &key) { return map.value(key).toMap(); }
QString json(const QVariantMap &map) { return QString::fromUtf8(QJsonDocument(QJsonObject::fromVariantMap(map)).toJson(QJsonDocument::Compact)); }
struct model::Deck readDeck(const QSqlQuery &q)
{ return {q.value(0).toString(),q.value(1).toString(),q.value(2).toString(),q.value(3).toString(),q.value(4).toString()}; }
struct model::Card readCard(const QSqlQuery &q)
{ return {q.value(0).toString(),q.value(1).toString(),q.value(2).toString(),q.value(3).toString(),q.value(4).toString(),q.value(5).toString(),q.value(6).toInt()}; }
struct model::Variant readVariant(const QSqlQuery &q)
{ return {q.value(0).toString(),q.value(1).toString(),q.value(2).toString(),q.value(3).toString(),q.value(4).toInt(),q.value(5).toDouble(),q.value(6).toDouble()}; }
struct model::Review readReview(const QSqlQuery &q)
{ return {q.value(0).toString(),q.value(1).toString(),q.value(2).toString(),q.value(3).toString(),q.value(4).toInt(),q.value(5).toDouble(),q.value(6).toDouble(),q.value(7).toString(),q.value(8).toString()}; }
QVariantList variantMaps(const QList<struct model::Variant> &variants)
{
    QVariantList result;
    for (const struct model::Variant &v : variants) result.append(model::toMap(v));
    return result;
}

}

DatabaseWorker::DatabaseWorker(QString path) : m_path(std::move(path)) {}
DatabaseWorker::~DatabaseWorker() { assert(!m_db.isValid()); }
void DatabaseWorker::assertThread() const { assert(thread() == QThread::currentThread()); }
void DatabaseWorker::shutdown()
{
    assertThread();
    if (m_db.isValid()) {
        m_db.close();
        m_db = QSqlDatabase();
        QSqlDatabase::removeDatabase(m_connectionName);
    }
}
void DatabaseWorker::fail(const QString &code, const QString &message, const QString &detail)
{
    emit operationFinished(false,message,{{"code",code},{"message",message},{"detail",detail}});
}
void DatabaseWorker::done(const QString &message) { emit operationFinished(true,message,{}); }
bool DatabaseWorker::ready()
{
    assertThread();
    m_sqlDetail.clear();
    if (m_db.isOpen()) return true;
    fail(QStringLiteral("STORAGE_UNAVAILABLE"),QStringLiteral("Storage is unavailable. Check the data directory and restart."));
    return false;
}
bool DatabaseWorker::execute(const QString &sql, const QVariantList &parameters)
{
    assertThread();
    QSqlQuery query(m_db);
    if (!query.prepare(sql)) { m_sqlDetail = query.lastError().text(); return false; }
    for (const QVariant &parameter : parameters) {
        if (parameter.metaType().id()==QMetaType::QString&&parameter.toString().isNull()) query.addBindValue(QStringLiteral(""));
        else query.addBindValue(parameter);
    }
    if (query.exec()) return true;
    m_sqlDetail = query.lastError().text();
    return false;
}
bool DatabaseWorker::mutate(const std::function<bool()> &work)
{
    assertThread();
    if (!m_db.transaction()) { m_sqlDetail = m_db.lastError().text(); return false; }
    if (!work()) { m_db.rollback(); return false; }
    if (m_db.commit()) return true;
    m_sqlDetail = m_db.lastError().text();
    m_db.rollback();
    return false;
}
void DatabaseWorker::initialize()
{
    assertThread();
    if (!QDir().mkpath(QFileInfo(m_path).absolutePath())) {
        fail("STORAGE_DIRECTORY","Could not create the data directory. Choose a writable directory.");return;
    }
    m_connectionName = QStringLiteral("betterflash-%1").arg(model::uuid());
    m_db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"),m_connectionName);
    m_db.setDatabaseName(m_path);
    if (!m_db.open()) { fail("STORAGE_OPEN","Could not open storage. Check the data directory permissions.",m_db.lastError().text());return; }
    if (!execute("PRAGMA foreign_keys=ON") || !execute("PRAGMA journal_mode=WAL")
        || !execute("PRAGMA busy_timeout=5000") || !migrate()) {
        fail("STORAGE_SCHEMA","Could not initialize storage. Preserve the database and inspect the details.",m_sqlDetail);
        shutdown();return;
    }
    QSqlQuery query(m_db);
    if (!query.exec("SELECT key,value FROM metadata")) { fail("STORAGE_METADATA","Could not load device metadata.",query.lastError().text());shutdown();return; }
    while (query.next()) {
        if (query.value(0).toString() == QStringLiteral("device_id")) m_deviceId = query.value(1).toString();
        else if (query.value(0).toString() == QStringLiteral("sync_cursor")) m_cursor = query.value(1).toLongLong();
    }
    if (!model::validUuid(m_deviceId)||m_cursor<0||m_cursor>9007199254740991LL) {
        fail("STORAGE_METADATA","Device metadata is invalid. Preserve the database and inspect the details.");shutdown();return;
    }
    if (publishSnapshot()) done("Ready");
}
bool DatabaseWorker::migrate()
{
    QSqlQuery versionQuery(m_db);
    if (!versionQuery.exec("PRAGMA user_version") || !versionQuery.next()) { m_sqlDetail = versionQuery.lastError().text();return false; }
    const int version = versionQuery.value(0).toInt();
    if (version > 3) { m_sqlDetail = QStringLiteral("Database version is newer than this application.");return false; }
    return mutate([&] {
        const QStringList schema{
            "CREATE TABLE IF NOT EXISTS decks(id TEXT PRIMARY KEY,name TEXT NOT NULL,description TEXT NOT NULL,created_at TEXT NOT NULL,parent_id TEXT NOT NULL DEFAULT '')",
            "CREATE TABLE IF NOT EXISTS notes(id TEXT PRIMARY KEY,deck_id TEXT NOT NULL REFERENCES decks(id) ON DELETE CASCADE,kind TEXT NOT NULL,front TEXT NOT NULL,back TEXT NOT NULL,tags TEXT NOT NULL,point_count INTEGER NOT NULL CHECK(point_count BETWEEN 1 AND 1000))",
            "CREATE TABLE IF NOT EXISTS review_variants(id TEXT PRIMARY KEY,card_id TEXT NOT NULL REFERENCES notes(id) ON DELETE CASCADE,variant_key TEXT NOT NULL,due TEXT NOT NULL,review_count INTEGER NOT NULL CHECK(review_count>=0),stability REAL NOT NULL CHECK(stability BETWEEN .25 AND 3650),difficulty REAL NOT NULL CHECK(difficulty BETWEEN 0 AND 1),UNIQUE(card_id,variant_key))",
            "CREATE INDEX IF NOT EXISTS variants_due ON review_variants(due)",
            "CREATE TABLE IF NOT EXISTS reviews(id TEXT PRIMARY KEY,card_id TEXT NOT NULL,variant_id TEXT NOT NULL,deck_name TEXT NOT NULL,grade INTEGER NOT NULL CHECK(grade BETWEEN 0 AND 4),recall_fraction REAL NOT NULL CHECK(recall_fraction BETWEEN 0 AND 1),response_seconds REAL NOT NULL CHECK(response_seconds BETWEEN 0 AND 86400),reviewed_at TEXT NOT NULL,due TEXT NOT NULL)",
            "CREATE INDEX IF NOT EXISTS reviews_time ON reviews(reviewed_at)",
            "CREATE TABLE IF NOT EXISTS metadata(key TEXT PRIMARY KEY,value TEXT NOT NULL)",
            "CREATE TABLE IF NOT EXISTS sync_outbox(position INTEGER PRIMARY KEY AUTOINCREMENT,id TEXT UNIQUE NOT NULL,device_id TEXT NOT NULL,event_type TEXT NOT NULL,payload TEXT NOT NULL,created_at TEXT NOT NULL,target_type TEXT NOT NULL,target_id TEXT NOT NULL)",
            "CREATE INDEX IF NOT EXISTS outbox_target ON sync_outbox(target_type,target_id)",
            "CREATE TABLE IF NOT EXISTS seen_events(id TEXT PRIMARY KEY,seq INTEGER NOT NULL DEFAULT 0)",
            "CREATE TABLE IF NOT EXISTS local_events(id TEXT PRIMARY KEY,target_type TEXT NOT NULL,target_id TEXT NOT NULL,event TEXT NOT NULL,echoed INTEGER NOT NULL DEFAULT 0)",
            "CREATE TABLE IF NOT EXISTS remote_pending(seq INTEGER PRIMARY KEY,event_id TEXT UNIQUE NOT NULL,event TEXT NOT NULL)",
            "CREATE TABLE IF NOT EXISTS entity_sequences(entity_type TEXT NOT NULL,entity_id TEXT NOT NULL,seq INTEGER NOT NULL,PRIMARY KEY(entity_type,entity_id))",
            "CREATE TABLE IF NOT EXISTS tombstones(entity_type TEXT NOT NULL,entity_id TEXT NOT NULL,seq INTEGER NOT NULL,PRIMARY KEY(entity_type,entity_id))"
        };
        for (const QString &statement : schema) if (!execute(statement)) return false;
        if (version < 3) {
            QSqlQuery columns(m_db);
            if (!columns.exec("PRAGMA table_info(decks)")) {m_sqlDetail=columns.lastError().text();return false;}
            bool hasParent=false;while (columns.next()) hasParent|=columns.value(1).toString()==QStringLiteral("parent_id");
            if (!hasParent&&!execute("ALTER TABLE decks ADD COLUMN parent_id TEXT NOT NULL DEFAULT ''")) return false;
        }
        if (!execute("CREATE INDEX IF NOT EXISTS decks_parent ON decks(parent_id)")) return false;
        if (!execute("INSERT OR IGNORE INTO metadata(key,value) VALUES('device_id',?)",{model::uuid()})
            || !execute("INSERT OR IGNORE INTO metadata(key,value) VALUES('sync_cursor','0')")) return false;
        QSqlQuery device(m_db);
        if (!device.exec("SELECT value FROM metadata WHERE key='device_id'") || !device.next()) {m_sqlDetail=device.lastError().text();return false;}
        m_deviceId = device.value(0).toString();
        if (version < 2 && m_db.tables().contains(QStringLiteral("cards"))) {
            QSqlQuery old(m_db);
            if (!old.exec("SELECT id,deck_id,kind,front,back,tags,point_count,due,review_count,stability,difficulty FROM cards")) {m_sqlDetail=old.lastError().text();return false;}
            QList<struct model::Card> migrated;
            while (old.next()) {
                const struct model::Card note = readCard(old);
                const auto valid = model::cardFromMap(model::toMap(note));
                if (!valid) {m_sqlDetail=valid.error();return false;}
                if (!upsertCard(note)) return false;
                const QDate oldDue = QDate::fromString(old.value(7).toString(),Qt::ISODate);
                const QString due = oldDue.isValid() ? oldDue.startOfDay(QTimeZone::systemTimeZone()).toUTC().toString(Qt::ISODateWithMs) : model::nowUtc();
                for (const QString &key : *model::variantKeys(note)) {
                    const struct model::Variant schedule{model::variantId(note.id,key),note.id,key,due,old.value(8).toInt(),std::clamp(old.value(9).toDouble(),.25,3650.0),std::clamp(old.value(10).toDouble(),0.0,1.0)};
                    if (!upsertVariant(schedule)) return false;
                }
                migrated.append(note);
            }
            QSqlQuery allDecks(m_db);
            if (!allDecks.exec("SELECT id,name,description,created_at,parent_id FROM decks")) {m_sqlDetail=allDecks.lastError().text();return false;}
            while (allDecks.next()) if (!enqueue("deck.upsert",{{"deck",model::toMap(readDeck(allDecks))}})) return false;
            for (const struct model::Card &note : migrated) if (!enqueue("card.upsert",cardPayload(note))) return false;
            if (m_db.tables().contains(QStringLiteral("history"))) {
                QSqlQuery history(m_db);
                if (!history.exec("SELECT id,card_id,deck_name,grade,recall_fraction,response_seconds,reviewed_at,due FROM history")) {m_sqlDetail=history.lastError().text();return false;}
                while (history.next()) {
                    const QString cardId = history.value(1).toString();
                    const int grade = QStringList{"Missed","Partial","Hard","Good","Easy"}.indexOf(history.value(3).toString());
                    const QDate oldDate = QDate::fromString(history.value(7).toString(),Qt::ISODate);
                    const auto reviewedAt = model::utcInstant(history.value(6).toString());
                    if (!model::validUuid(cardId) || grade < 0 || !oldDate.isValid() || !reviewedAt) {m_sqlDetail="Legacy review is invalid.";return false;}
                    const struct model::Review review{history.value(0).toString(),cardId,model::variantId(cardId,"forward"),history.value(2).toString(),grade,history.value(4).toDouble(),history.value(5).toDouble(),*reviewedAt,oldDate.startOfDay(QTimeZone::systemTimeZone()).toUTC().toString(Qt::ISODateWithMs)};
                    if (!model::reviewFromMap(model::toMap(review)) || !insertReview(review)) return false;
                }
            }
        }
        QSqlQuery pending(m_db);
        if (!pending.exec("SELECT id,device_id,event_type,payload,created_at,target_type,target_id FROM sync_outbox")) {m_sqlDetail=pending.lastError().text();return false;}
        while (pending.next()) {
            const QJsonDocument payload=QJsonDocument::fromJson(pending.value(3).toString().toUtf8());
            if (!payload.isObject()) {m_sqlDetail="Stored upload payload is invalid.";return false;}
            const QVariantMap event{{"id",pending.value(0)},{"deviceId",pending.value(1)},{"type",pending.value(2)},{"payload",payload.object().toVariantMap()},{"createdAt",pending.value(4)}};
            if (!execute("INSERT OR IGNORE INTO local_events(id,target_type,target_id,event) VALUES(?,?,?,?)",{pending.value(0),pending.value(5),pending.value(6),json(event)})) return false;
        }
        return execute("PRAGMA user_version=3");
    });
}
std::optional<struct model::Deck> DatabaseWorker::deck(const QString &id)
{
    QSqlQuery q(m_db);q.prepare("SELECT id,name,description,created_at,parent_id FROM decks WHERE id=?");q.addBindValue(id);
    if (!q.exec()) {m_sqlDetail=q.lastError().text();return std::nullopt;}
    return q.next() ? std::optional<struct model::Deck>(readDeck(q)) : std::nullopt;
}
std::optional<struct model::Card> DatabaseWorker::card(const QString &id)
{
    QSqlQuery q(m_db);q.prepare("SELECT id,deck_id,kind,front,back,tags,point_count FROM notes WHERE id=?");q.addBindValue(id);
    if (!q.exec()) {m_sqlDetail=q.lastError().text();return std::nullopt;}
    return q.next() ? std::optional<struct model::Card>(readCard(q)) : std::nullopt;
}
std::optional<struct model::Variant> DatabaseWorker::variant(const QString &id)
{
    QSqlQuery q(m_db);q.prepare("SELECT id,card_id,variant_key,due,review_count,stability,difficulty FROM review_variants WHERE id=?");q.addBindValue(id);
    if (!q.exec()) {m_sqlDetail=q.lastError().text();return std::nullopt;}
    return q.next() ? std::optional<struct model::Variant>(readVariant(q)) : std::nullopt;
}
QList<struct model::Variant> DatabaseWorker::cardVariants(const QString &id)
{
    QList<struct model::Variant> result;
    QSqlQuery q(m_db);q.prepare("SELECT id,card_id,variant_key,due,review_count,stability,difficulty FROM review_variants WHERE card_id=? ORDER BY variant_key");q.addBindValue(id);
    if (!q.exec()) {m_sqlDetail=q.lastError().text();return result;}
    while (q.next()) result.append(readVariant(q));
    return result;
}
bool DatabaseWorker::upsertDeck(const struct model::Deck &r)
{ return execute("INSERT INTO decks(id,name,description,created_at,parent_id) VALUES(?,?,?,?,?) ON CONFLICT(id) DO UPDATE SET name=excluded.name,description=excluded.description,created_at=excluded.created_at,parent_id=excluded.parent_id",{r.id,r.name,r.description,r.createdAt,r.parentId}); }
bool DatabaseWorker::upsertCard(const struct model::Card &r)
{ return execute("INSERT INTO notes(id,deck_id,kind,front,back,tags,point_count) VALUES(?,?,?,?,?,?,?) ON CONFLICT(id) DO UPDATE SET deck_id=excluded.deck_id,kind=excluded.kind,front=excluded.front,back=excluded.back,tags=excluded.tags,point_count=excluded.point_count",{r.id,r.deckId,r.kind,r.front,r.back,r.tags,r.pointCount}); }
bool DatabaseWorker::upsertVariant(const struct model::Variant &r)
{ return execute("INSERT INTO review_variants(id,card_id,variant_key,due,review_count,stability,difficulty) VALUES(?,?,?,?,?,?,?) ON CONFLICT(id) DO UPDATE SET due=excluded.due,review_count=excluded.review_count,stability=excluded.stability,difficulty=excluded.difficulty",{r.id,r.cardId,r.key,r.due,r.reviewCount,r.stability,r.difficulty})
    &&execute("DELETE FROM tombstones WHERE entity_type='variant' AND entity_id=?",{r.id}); }
bool DatabaseWorker::insertReview(const struct model::Review &r)
{
    QSqlQuery existing(m_db);existing.prepare("SELECT id,card_id,variant_id,deck_name,grade,recall_fraction,response_seconds,reviewed_at,due FROM reviews WHERE id=?");existing.addBindValue(r.id);
    if (!existing.exec()) {m_sqlDetail=existing.lastError().text();return false;}
    if (existing.next()) {
        if (model::toMap(readReview(existing)) == model::toMap(r)) return true;
        m_sqlDetail="A review identifier already exists with different immutable content.";return false;
    }
    return execute("INSERT INTO reviews(id,card_id,variant_id,deck_name,grade,recall_fraction,response_seconds,reviewed_at,due) VALUES(?,?,?,?,?,?,?,?,?)",{r.id,r.cardId,r.variantId,r.deckName,r.grade,r.recallFraction,r.responseSeconds,r.reviewedAt,r.due});
}
std::optional<struct model::Review> DatabaseWorker::review(const QString &id)
{
    QSqlQuery q(m_db);
    q.prepare("SELECT id,card_id,variant_id,deck_name,grade,recall_fraction,response_seconds,reviewed_at,due FROM reviews WHERE id=?");
    q.addBindValue(id);
    if (!q.exec()) {m_sqlDetail=q.lastError().text();return std::nullopt;}
    return q.next()?std::optional<struct model::Review>(readReview(q)):std::nullopt;
}
bool DatabaseWorker::correctReview(const struct model::Review &record,const struct model::Review &previous)
{
    assert(record.id==previous.id&&record.cardId==previous.cardId&&record.variantId==previous.variantId);
    assert(record.reviewedAt==previous.reviewedAt&&record.responseSeconds==previous.responseSeconds&&record.deckName==previous.deckName);
    QSqlQuery q(m_db);
    q.prepare("UPDATE reviews SET grade=?,recall_fraction=?,due=? WHERE id=? AND card_id=? AND variant_id=? AND reviewed_at=? AND response_seconds=? AND deck_name=? AND grade=? AND recall_fraction=? AND due=?");
    for (const QVariant &value:QVariantList{record.grade,record.recallFraction,record.due,previous.id,previous.cardId,previous.variantId,previous.reviewedAt,previous.responseSeconds,previous.deckName,previous.grade,previous.recallFraction,previous.due}) q.addBindValue(value);
    if (!q.exec()) {m_sqlDetail=q.lastError().text();return false;}
    if (q.numRowsAffected()!=1) {m_sqlDetail="The review changed or no longer exists. Refresh before changing its result.";return false;}
    return true;
}
bool DatabaseWorker::replaceVariants(const struct model::Card &record, const QList<struct model::Variant> &variants)
{
    QSet<QString> keep;
    for (const struct model::Variant &v : variants) {keep.insert(v.id);if (!upsertVariant(v)) return false;}
    for (const struct model::Variant &v : cardVariants(record.id))
        if (!keep.contains(v.id) && (!tombstone("variant",v.id)||!execute("DELETE FROM review_variants WHERE id=?",{v.id}))) return false;
    return m_sqlDetail.isEmpty();
}
QVariantMap DatabaseWorker::cardPayload(const struct model::Card &record)
{ return {{"card",model::toMap(record)},{"variants",variantMaps(cardVariants(record.id))}}; }
bool DatabaseWorker::enqueue(const QString &type, const QVariantMap &payload)
{
    QString targetType,targetId;
    if (type.startsWith(QStringLiteral("deck."))) {targetType="deck";targetId=type.endsWith(QStringLiteral("delete"))?payload.value("id").toString():object(payload,"deck").value("id").toString();}
    else if (type.startsWith(QStringLiteral("card."))) {targetType="card";targetId=type.endsWith(QStringLiteral("delete"))?payload.value("id").toString():object(payload,"card").value("id").toString();}
    else if (type==QStringLiteral("review.add")&&payload.value("historyOnly").toBool()) {targetType="history";targetId=object(payload,"review").value("id").toString();}
    else {targetType="variant";targetId=object(payload,"variant").value("id").toString();}
    const QString id = model::uuid();
    const QString createdAt=model::nowUtc();
    const QVariantMap event{{"id",id},{"deviceId",m_deviceId},{"type",type},{"payload",payload},{"createdAt",createdAt}};
    const QString encoded=json(event);
    if (encoded.toUtf8().size()>model::maxSyncEventBytes) {m_sqlDetail="This change exceeds the 3 MiB sync event limit. Shorten card content or split it into smaller cards.";return false;}
    const auto valid=betterflash::protocol::validateEvent(event);
    if (!valid) {m_sqlDetail=valid.error();return false;}
    return execute("INSERT INTO sync_outbox(id,device_id,event_type,payload,created_at,target_type,target_id) VALUES(?,?,?,?,?,?,?)",{id,m_deviceId,type,json(payload),createdAt,targetType,targetId})
        && execute("INSERT INTO seen_events(id,seq) VALUES(?,0)",{id})
        && execute("INSERT INTO local_events(id,target_type,target_id,event) VALUES(?,?,?,?)",{id,targetType,targetId,encoded});
}
bool DatabaseWorker::validateMedia(const struct model::Card &record)
{
    const auto names=model::cardMediaReferences(record);
    if (!names) {m_sqlDetail=names.error();return false;}
    const auto images=model::readBackupMedia(QDir(QFileInfo(m_path).absolutePath()).filePath("media"),*names);
    if (!images) {m_sqlDetail=images.error();return false;}
    return true;
}
bool DatabaseWorker::tombstone(const QString &type, const QString &id, qint64 seq)
{ return execute("INSERT INTO tombstones(entity_type,entity_id,seq) VALUES(?,?,?) ON CONFLICT(entity_type,entity_id) DO UPDATE SET seq=MAX(seq,excluded.seq)",{type,id,seq}); }
bool DatabaseWorker::isDeleted(const QString &type, const QString &id)
{
    QSqlQuery q(m_db);q.prepare("SELECT 1 FROM tombstones WHERE entity_type=? AND entity_id=?");q.addBindValue(type);q.addBindValue(id);
    if (!q.exec()) {m_sqlDetail=q.lastError().text();return false;}
    return q.next();
}
bool DatabaseWorker::eraseCard(const QString &id, qint64 seq)
{ return tombstone("card",id,seq) && execute("DELETE FROM notes WHERE id=?",{id}); }
bool DatabaseWorker::eraseDeck(const QString &id, qint64 seq)
{
    QSqlQuery descendants(m_db);descendants.prepare("WITH RECURSIVE descendants(id) AS (SELECT ? UNION SELECT d.id FROM decks d JOIN descendants ON d.parent_id=descendants.id) SELECT id FROM descendants");descendants.addBindValue(id);
    if (!descendants.exec()) {m_sqlDetail=descendants.lastError().text();return false;}
    QStringList deckIds;while (descendants.next()) deckIds.append(descendants.value(0).toString());
    for (const QString &deckId : deckIds) {
        QSqlQuery cards(m_db);cards.prepare("SELECT id FROM notes WHERE deck_id=?");cards.addBindValue(deckId);
        if (!cards.exec()) {m_sqlDetail=cards.lastError().text();return false;}
        QStringList cardIds;while (cards.next()) cardIds.append(cards.value(0).toString());
        for (const QString &cardId : cardIds) if (!eraseCard(cardId,seq)) return false;
    }
    for (const QString &deckId : deckIds) if (!tombstone("deck",deckId,seq)||!execute("DELETE FROM decks WHERE id=?",{deckId})) return false;
    return true;
}
bool DatabaseWorker::validDeckParent(const QString &id,const QString &parentId)
{
    if (parentId.isEmpty()) return true;
    if (!model::validUuid(parentId)||parentId==id||!deck(parentId)) return false;
    QSet<QString> seen{id};QString current=parentId;
    while (!current.isEmpty()) {
        if (seen.contains(current)) return false;
        seen.insert(current);
        QSqlQuery query(m_db);query.prepare("SELECT parent_id FROM decks WHERE id=?");query.addBindValue(current);
        if (!query.exec()) {m_sqlDetail=query.lastError().text();return false;}
        if (!query.next()) return false;
        current=query.value(0).toString();
    }
    return true;
}

bool DatabaseWorker::publishSnapshot()
{
    assertThread();
    QVariantList decks,cards,history;
    const QString now=model::nowUtc();
    QSqlQuery q(m_db);
    q.prepare("SELECT d.id,d.name,d.description,d.created_at,d.parent_id,COUNT(DISTINCT n.id),COUNT(v.id),SUM(CASE WHEN v.due<=? THEN 1 ELSE 0 END),SUM(CASE WHEN v.due>? THEN 1 ELSE 0 END),SUM(CASE WHEN v.due<=? AND v.review_count=0 THEN 1 ELSE 0 END),SUM(CASE WHEN v.due<=? AND v.review_count>0 THEN 1 ELSE 0 END) FROM decks d LEFT JOIN notes n ON n.deck_id=d.id LEFT JOIN review_variants v ON v.card_id=n.id GROUP BY d.id ORDER BY d.name,d.id");q.addBindValue(now);q.addBindValue(now);q.addBindValue(now);q.addBindValue(now);
    if (!q.exec()) {fail("STORAGE_READ","Could not load decks. Try refreshing.",q.lastError().text());return false;}
    QHash<QString,int> deckIndexes;QHash<QString,QString> deckParents;
    while (q.next()) {
        QVariantMap map=model::toMap(readDeck(q));
        const QString id=map.value("id").toString();deckIndexes.insert(id,decks.size());deckParents.insert(id,map.value("parentId").toString());
        map.insert("ownCardCount",q.value(5).toInt());map.insert("ownVariantCount",q.value(6).toInt());map.insert("ownDueCount",q.value(7).toInt());map.insert("ownLaterCount",q.value(8).toInt());map.insert("ownNewDueCount",q.value(9).toInt());map.insert("ownReviewDueCount",q.value(10).toInt());map.insert("cardCount",q.value(5).toInt());map.insert("totalCount",q.value(5).toInt());map.insert("variantCount",q.value(6).toInt());map.insert("dueCount",q.value(7).toInt());map.insert("laterCount",q.value(8).toInt());map.insert("newDueCount",q.value(9).toInt());map.insert("reviewDueCount",q.value(10).toInt());decks.append(map);
    }
    QHash<QString,int> childCounts;QList<QString> ready;
    for (const QString &id : deckIndexes.keys()) childCounts.insert(id,0);
    for (const QString &id : deckIndexes.keys()) {
        const QString parent=deckParents.value(id);
        if (parent.isEmpty()) continue;
        if (childCounts.contains(parent)) childCounts[parent]++;
        else {fail("STORAGE_HIERARCHY","A deck refers to a missing parent. Repair the database and refresh.");return false;}
    }
    for (const QString &id : deckIndexes.keys()) if (childCounts.value(id)==0) ready.append(id);
    int processed=0;
    while (!ready.isEmpty()) {
        const QString current=ready.takeLast();++processed;
        const QString parent=deckParents.value(current);if (parent.isEmpty()) continue;
        QVariantMap child=decks.at(deckIndexes.value(current)).toMap();QVariantMap aggregate=decks.at(deckIndexes.value(parent)).toMap();
        for (const QString &key : {QStringLiteral("cardCount"),QStringLiteral("totalCount"),QStringLiteral("variantCount"),QStringLiteral("dueCount"),QStringLiteral("laterCount"),QStringLiteral("newDueCount"),QStringLiteral("reviewDueCount")}) aggregate[key]=aggregate.value(key).toInt()+child.value(key).toInt();
        decks[deckIndexes.value(parent)]=aggregate;
        const int remaining=childCounts.value(parent)-1;childCounts[parent]=remaining;if (remaining==0) ready.append(parent);
    }
    if (processed!=deckIndexes.size()) {fail("STORAGE_HIERARCHY","The deck hierarchy contains a cycle. Repair the database and refresh.");return false;}
    QHash<QString,QList<struct model::Variant>> variants;
    if (!q.exec("SELECT id,card_id,variant_key,due,review_count,stability,difficulty FROM review_variants ORDER BY variant_key")) {fail("STORAGE_READ","Could not load review variants. Try refreshing.",q.lastError().text());return false;}
    while (q.next()) {const struct model::Variant schedule=readVariant(q);variants[schedule.cardId].append(schedule);}
    if (!q.exec("SELECT id,deck_id,kind,front,back,tags,point_count FROM notes ORDER BY id")) {fail("STORAGE_READ","Could not load cards. Try refreshing.",q.lastError().text());return false;}
    while (q.next()) {
        const struct model::Card note=readCard(q);
        QVariantMap map=model::toMap(note);
        const QList<struct model::Variant> schedules=variants.value(note.id);
        map.insert("variants",variantMaps(schedules));
        QString earliest;qint64 reviewCount=0;int dueCount=0;
        for (const struct model::Variant &v:schedules) {
            if (earliest.isEmpty()||v.due<earliest) earliest=v.due;
            reviewCount+=v.reviewCount;
            if (v.due<=now) ++dueCount;
        }
        map.insert("due",earliest);map.insert("reviewCount",reviewCount);map.insert("dueCount",dueCount);cards.append(map);
    }
    if (!q.exec("SELECT id,card_id,variant_id,deck_name,grade,recall_fraction,response_seconds,reviewed_at,due FROM reviews ORDER BY reviewed_at DESC,id DESC LIMIT 200")) {fail("STORAGE_READ","Could not load review history. Try refreshing.",q.lastError().text());return false;}
    while (q.next()) {
        const struct model::Review review=readReview(q);
        QVariantMap map=model::toMap(review);map.insert("gradeLabel",model::gradeLabel(review.grade));history.append(map);
    }
    emit snapshotReady(decks,cards,history);
    return true;
}
void DatabaseWorker::snapshot()
{
    if (!ready()) return;
    m_queueDirty=true;
    if (publishSnapshot()&&publishQueue(false)) done("Collection refreshed");
}
QVariantMap DatabaseWorker::reviewCard(const struct model::Card &note,const struct model::Variant &schedule,const QString &deckName)
{
    QVariantMap result=model::toMap(note);
    result.insert("variantId",schedule.id);result.insert("variantKey",schedule.key);
    result.insert("due",schedule.due);result.insert("reviewCount",schedule.reviewCount);
    result.insert("stability",schedule.stability);result.insert("difficulty",schedule.difficulty);
    result.insert("question",model::question(note,schedule.key));result.insert("answer",model::answer(note,schedule.key));
    result.insert("readingBudgetSeconds",model::readingBudget(result.value("question").toString()));result.insert("deckName",deckName);
    return result;
}
QVariantMap DatabaseWorker::sessionCard(const QString &variantId, const QVariantMap &card) const
{
    QVariantMap result=card;
    const auto grade=m_sessionGrades.constFind(variantId);
    const bool graded=grade!=m_sessionGrades.cend();
    result.insert("sessionGrade",graded?grade->savedReview.grade:-1);
    result.insert("sessionRecall",graded?grade->savedReview.recallFraction:-1.0);
    result.insert("sessionReviewId",graded?grade->savedReview.id:QString());
    result.insert("responseSeconds",graded?grade->savedReview.responseSeconds:0.0);
    return result;
}
std::optional<QVariantMap> DatabaseWorker::reviewCardByVariant(const QString &variantId)
{
    const auto schedule=variant(variantId);
    const auto note=schedule?card(schedule->cardId):std::nullopt;
    const auto sourceDeck=note?deck(note->deckId):std::nullopt;
    if (!schedule||!note||!sourceDeck) return std::nullopt;
    const auto keys=model::variantKeys(*note);
    if (!keys||!keys->contains(schedule->key)) return std::nullopt;
    return reviewCard(*note,*schedule,sourceDeck->name);
}
bool DatabaseWorker::publishQueue(bool resetCurrent)
{
    QVariantList cards;
    if (m_queueDirty&&!m_sessionOrder.isEmpty()) {
        const QSet<QString> members(m_sessionOrder.cbegin(),m_sessionOrder.cend());
        const QString now=model::nowUtc();
        QHash<QString,QVariantMap> cache;
        QSqlQuery q(m_db);
        q.prepare("SELECT v.id,v.card_id,v.variant_key,v.due,v.review_count,v.stability,v.difficulty,n.deck_id,n.kind,n.front,n.back,n.tags,n.point_count,d.name FROM review_variants v JOIN notes n ON n.id=v.card_id JOIN decks d ON d.id=n.deck_id");
        if (!q.exec()) {fail("REVIEW_LOAD","Could not load the review queue. Refresh and try again.",q.lastError().text());return false;}
        while (q.next()) {
            const struct model::Variant schedule=readVariant(q);
            if (!members.contains(schedule.id)||(!m_sessionGrades.contains(schedule.id)&&schedule.due>now)) continue;
            const struct model::Card note{schedule.cardId,q.value(7).toString(),q.value(8).toString(),q.value(9).toString(),q.value(10).toString(),q.value(11).toString(),q.value(12).toInt()};
            const auto keys=model::variantKeys(note);
            if (!keys||!keys->contains(schedule.key)) continue;
            cache.insert(schedule.id,reviewCard(note,schedule,q.value(13).toString()));
        }
        m_queueCache=std::move(cache);
    }
    m_queueDirty=false;
    for (auto it=m_sessionOrder.begin();it!=m_sessionOrder.end();) {
        const auto cached=m_queueCache.constFind(*it);
        if (cached==m_queueCache.cend()) {const QString missing=*it;it=m_sessionOrder.erase(it);m_queue.removeAll(missing);m_sessionGrades.remove(missing);continue;}
        cards.append(sessionCard(*it,*cached));++it;
    }
    m_sessionTotal=m_sessionOrder.size();
    if (m_sessionOrder.isEmpty()) {m_sessionActive=false;m_queueCache.clear();m_selectedVariant.clear();}
    else if (!m_sessionOrder.contains(m_selectedVariant)) m_selectedVariant=m_sessionOrder.first();
    const QVariantMap selectedCard=m_selectedVariant.isEmpty()?QVariantMap():sessionCard(m_selectedVariant,m_queueCache.value(m_selectedVariant));
    emit reviewQueueReady(cards,m_sessionTotal,resetCurrent,selectedCard,m_sessionGrades.contains(m_selectedVariant),m_selectedVariant);
    return true;
}
void DatabaseWorker::advanceAfterRemoval(qsizetype position)
{
    assert(position>=0);
    m_selectedVariant=m_queue.isEmpty()?QString():m_queue.at(position<m_queue.size()?position:0);
}

void DatabaseWorker::createDeck(const QString &name, const QString &description, const QString &parentId)
{
    if (!ready()) return;
    const struct model::Deck record{model::uuid(),name.trimmed(),description,model::nowUtc(),parentId};
    const auto valid=model::deckFromMap(model::toMap(record));
    if (!valid) {fail("INVALID_DECK","Enter a deck name of at most 256 characters and a description of at most 65536 characters.",valid.error());return;}
    if (!validDeckParent(record.id,record.parentId)) {fail("INVALID_DECK_PARENT","Choose an existing parent deck that is not this deck or one of its descendants.");return;}
    if (!mutate([&]{return upsertDeck(record)&&enqueue("deck.upsert",{{"deck",model::toMap(record)}});})) {fail("DECK_SAVE","Could not save the deck. Check storage and try again.",m_sqlDetail);return;}
    if (publishSnapshot()) done("Deck created");
}
void DatabaseWorker::updateDeck(const QString &id, const QString &name, const QString &description, const QString &parentId)
{
    if (!ready()) return;
    const auto existing=deck(id);
    if (!existing) {fail("DECK_NOT_FOUND","This deck no longer exists. Refresh the collection.",m_sqlDetail);return;}
    const struct model::Deck record{id,name.trimmed(),description,existing->createdAt,parentId};
    const auto valid=model::deckFromMap(model::toMap(record));
    if (!valid) {fail("INVALID_DECK","Enter a valid deck name and description.",valid.error());return;}
    if (!validDeckParent(record.id,record.parentId)) {fail("INVALID_DECK_PARENT","Choose an existing parent deck that is not this deck or one of its descendants.");return;}
    if (!mutate([&]{return upsertDeck(record)&&enqueue("deck.upsert",{{"deck",model::toMap(record)}});})) {fail("DECK_SAVE","Could not save the deck. Check storage and try again.",m_sqlDetail);return;}
    m_queueDirty=true;
    if (publishSnapshot()&&publishQueue(false)) done("Deck updated");
}
void DatabaseWorker::deleteDeck(const QString &id)
{
    if (!ready()) return;
    if (!model::validUuid(id)) {fail("INVALID_DECK_ID","Choose a valid deck to delete.");return;}
    QSqlQuery hierarchy(m_db);hierarchy.prepare("WITH RECURSIVE descendants(id,parent_id) AS (SELECT id,parent_id FROM decks WHERE id=? UNION SELECT d.id,d.parent_id FROM decks d JOIN descendants ON d.parent_id=descendants.id) SELECT id,parent_id FROM descendants");hierarchy.addBindValue(id);
    if (!hierarchy.exec()) {fail("DECK_DELETE","Could not inspect the deck subtree. Check storage and try again.",hierarchy.lastError().text());return;}
    QHash<QString,QString> parents;while (hierarchy.next()) parents.insert(hierarchy.value(0).toString(),hierarchy.value(1).toString());
    if (parents.isEmpty()) parents.insert(id,QString());
    QStringList cardIds;
    for (const QString &deckId : parents.keys()) {
        QSqlQuery cards(m_db);cards.prepare("SELECT id FROM notes WHERE deck_id=?");cards.addBindValue(deckId);
        if (!cards.exec()) {fail("DECK_DELETE","Could not inspect the deck cards. Check storage and try again.",cards.lastError().text());return;}
        while (cards.next()) cardIds.append(cards.value(0).toString());
    }
    QStringList deckIds=parents.keys();
    const auto depth=[&](const QString &deckId) {int result=0;QSet<QString> seen;QString current=deckId;while (!current.isEmpty()&&!seen.contains(current)&&parents.contains(current)) {seen.insert(current);current=parents.value(current);++result;}return result;};
    std::sort(deckIds.begin(),deckIds.end(),[&](const QString &left,const QString &right){return depth(left)>depth(right);});
    if (!mutate([&] {
        if (!eraseDeck(id)) return false;
        for (const QString &cardId : cardIds) if (!enqueue("card.delete",{{"id",cardId}})) return false;
        for (const QString &deckId : deckIds) if (!enqueue("deck.delete",{{"id",deckId}})) return false;
        return true;
    })) {fail("DECK_DELETE","Could not delete the deck. Check storage and try again.",m_sqlDetail);return;}
    m_queueDirty=true;
    if (publishSnapshot()&&publishQueue(false)) done("Deck deleted");
}
void DatabaseWorker::saveCard(const QString &id,const QString &deckId,const QString &kind,const QString &front,const QString &back,const QString &tags,int points)
{
    if (!ready()) return;
    const struct model::Card record{id.isEmpty()?model::uuid():id,deckId,kind,front,back,tags,points};
    const auto valid=model::cardFromMap(model::toMap(record));
    if (!valid) {fail(kind==QStringLiteral("cloze")?"INVALID_CLOZE":"INVALID_CARD","Could not save the card. Correct the card fields.",valid.error());return;}
    if (!validateMedia(record)) {fail("CARD_MEDIA","Could not save the card images. Restore missing images, insert them again, or split cards containing more than 64 MiB of images.",m_sqlDetail);return;}
    if (!deck(deckId) || isDeleted("deck",deckId)) {fail("DECK_NOT_FOUND","Choose an existing deck for this card.",m_sqlDetail);return;}
    if (!id.isEmpty() && (!card(id) || isDeleted("card",id))) {fail("CARD_NOT_FOUND","This card no longer exists. Refresh the collection.",m_sqlDetail);return;}
    QList<struct model::Variant> schedules;
    const QList<struct model::Variant> existing=cardVariants(record.id);
    for (const QString &key : *model::variantKeys(record)) {
        const QString variantId=model::variantId(record.id,key);
        const auto found=std::find_if(existing.begin(),existing.end(),[&](const struct model::Variant &v){return v.id==variantId;});
        schedules.append(found==existing.end()?model::Variant{variantId,record.id,key,model::nowUtc()}:*found);
    }
    if (!mutate([&]{return upsertCard(record)&&replaceVariants(record,schedules)&&enqueue("card.upsert",cardPayload(record));})) {fail("CARD_SAVE","Could not save the card. Check storage and try again.",m_sqlDetail);return;}
    m_queueDirty=true;
    if (publishSnapshot()&&publishQueue(false)) done("Card saved");
}
void DatabaseWorker::replaceCardWithAtomicCards(const QVariantMap &expectedSource,const QVariantList &proposals)
{
    const QString sourceId=expectedSource.value("id").toString();
    if (!ready()) {emit atomicSplitApplied(sourceId,false,{});return;}
    const auto reject=[&](const QString &code,const QString &message,const QString &detail={}) {
        fail(code,message,detail);emit atomicSplitApplied(sourceId,false,{});
    };
    const auto expected=model::cardFromMap(expectedSource);
    if (!expected) {reject("INVALID_ATOMIC_SPLIT","Could not replace the card. Choose a valid source card.",expected.error());return;}
    const auto replacements=model::atomicCardsFromProposals(*expected,proposals);
    if (!replacements) {reject("INVALID_ATOMIC_SPLIT","Could not replace the card. Correct the proposed cards.",replacements.error());return;}
    if (!validateMedia(*expected)) {reject("ATOMIC_SPLIT_MEDIA","Could not replace the card images. Restore the attached images or insert them again.",m_sqlDetail);return;}
    bool stale=false;
    const bool committed=mutate([&] {
        const auto stored=card(sourceId);
        if (!m_sqlDetail.isEmpty()) return false;
        if (!stored||model::toMap(*stored)!=model::toMap(*expected)) {stale=true;return false;}
        if (!eraseCard(sourceId)||!enqueue("card.delete",{{"id",sourceId}})) return false;
        for (const struct model::Card &replacement:*replacements) {
            const struct model::Variant schedule{model::variantId(replacement.id,"forward"),replacement.id,"forward",model::nowUtc()};
            if (!upsertCard(replacement)||!upsertVariant(schedule)||!enqueue("card.upsert",cardPayload(replacement))) return false;
        }
        return true;
    });
    if (!committed) {
        if (stale) reject("ATOMIC_SPLIT_STALE","The original card changed or was deleted. Generate a new proposal from its current content.");
        else reject("ATOMIC_SPLIT_SAVE","Could not replace the card. The original card and pending sync changes were preserved. Check storage and retry.",m_sqlDetail);
        return;
    }
    QStringList newIds;
    for (const struct model::Card &replacement:*replacements) newIds.append(replacement.id);
    m_queueDirty=true;
    if (publishSnapshot()&&publishQueue(false)) done(QStringLiteral("Original replaced with %1 focused cards").arg(newIds.size()));
    emit atomicSplitApplied(sourceId,true,newIds);
}
void DatabaseWorker::deleteCard(const QString &id)
{
    if (!ready()) return;
    if (!model::validUuid(id)) {fail("INVALID_CARD_ID","Choose a valid card to delete.");return;}
    if (!mutate([&]{return eraseCard(id)&&enqueue("card.delete",{{"id",id}});})) {fail("CARD_DELETE","Could not delete the card. Check storage and try again.",m_sqlDetail);return;}
    m_queueDirty=true;
    if (publishSnapshot()&&publishQueue(false)) done("Card deleted");
}
void DatabaseWorker::beginReview(const QString &deckId)
{
    if (!ready()) return;
    if (!deckId.isEmpty() && (!model::validUuid(deckId)||!deck(deckId))) {fail("DECK_NOT_FOUND","Choose an existing deck to review.");return;}
    QSqlQuery q(m_db);
    q.prepare("SELECT v.id FROM review_variants v JOIN notes n ON n.id=v.card_id WHERE v.due<=?"+QString(deckId.isEmpty()?"":" AND n.deck_id IN (WITH RECURSIVE descendants(id) AS (SELECT id FROM decks WHERE id=? UNION ALL SELECT d.id FROM decks d JOIN descendants ON d.parent_id=descendants.id) SELECT id FROM descendants)")+" ORDER BY v.due,n.id,v.variant_key");
    q.addBindValue(model::nowUtc());if (!deckId.isEmpty()) q.addBindValue(deckId);
    if (!q.exec()) {fail("REVIEW_START","Could not start review. Refresh and try again.",q.lastError().text());return;}
    m_queue.clear();m_sessionOrder.clear();m_queueCache.clear();m_sessionGrades.clear();m_selectedVariant.clear();m_queueDirty=true;while (q.next()) { const QString id=q.value(0).toString(); m_queue.append(id); m_sessionOrder.append(id); }
    if (!m_queue.isEmpty()) m_selectedVariant=m_queue.first();
    m_sessionTotal=m_queue.size();m_sessionActive=!m_queue.isEmpty();
    if (publishQueue(true)) done(m_queue.isEmpty()?"No cards are due in this deck":"Review started");
}
void DatabaseWorker::selectReviewCard(const QString &variantId)
{
    if (!ready()) return;
    if (!m_sessionActive||m_sessionOrder.isEmpty()) {fail("REVIEW_INACTIVE","Start a review before choosing a card.");return;}
    if (!model::validUuid(variantId)||!m_sessionOrder.contains(variantId)) {
        fail("REVIEW_STALE","This card is no longer in the review session. Choose a card in the current queue.");return;
    }
    const auto selected=reviewCardByVariant(variantId);
    if (!m_sqlDetail.isEmpty()) {fail("REVIEW_LOAD","Could not load the selected card. Refresh and try again.",m_sqlDetail);return;}
    if (!selected) {
        m_queueDirty=true;
        if (publishQueue(false)) fail("REVIEW_STALE","This card is no longer available for review. Choose another card.");
        return;
    }
    if (variantId==m_selectedVariant) {done();return;}
    m_selectedVariant=variantId;
    m_queueCache.insert(variantId,*selected);
    assert(m_queue.size()<=m_sessionTotal);
    if (publishQueue(true)) done(m_sessionGrades.contains(variantId)?"Reviewed card selected":"Review card selected");
}
void DatabaseWorker::endReview()
{
    if (!ready()) return;
    m_queue.clear();m_sessionOrder.clear();m_queueCache.clear();m_sessionGrades.clear();
    m_sessionActive=false;m_selectedVariant.clear();m_queueDirty=true;
    emit reviewQueueReady({},m_sessionTotal,true,{},false,{});done("Review ended");
}
void DatabaseWorker::defer()
{
    if (!ready()) return;
    if (!m_sessionActive||m_queue.isEmpty()) {fail("REVIEW_INACTIVE","Start a review before deferring a card.");return;}
    const qsizetype position=m_queue.indexOf(m_selectedVariant);
    if (position<0) {fail("REVIEW_COMPLETED","This card is already graded. Choose an ungraded card to defer.");return;}
    const QString deferred=m_queue.takeAt(position);
    advanceAfterRemoval(position);
    m_queue.append(deferred);
    const qsizetype sessionPosition=m_sessionOrder.indexOf(deferred);
    assert(sessionPosition>=0);
    m_sessionOrder.removeAt(sessionPosition);m_sessionOrder.append(deferred);
    if (m_selectedVariant.isEmpty()) m_selectedVariant=deferred;
    if (publishQueue(true)) done("Card moved to the queue end");
}
void DatabaseWorker::postpone(const QString &date)
{
    if (!ready()) return;
    const QDate day=QDate::fromString(date,Qt::ISODate);
    if (!day.isValid()||date!=day.toString(Qt::ISODate)||day<=QDate::currentDate()) {fail("INVALID_POSTPONE_DATE","Choose a later date in YYYY-MM-DD format.");return;}
    if (!m_sessionActive||m_queue.isEmpty()) {fail("REVIEW_INACTIVE","Start a review before postponing a card.");return;}
    const qsizetype position=m_queue.indexOf(m_selectedVariant);
    if (position<0) {fail("REVIEW_COMPLETED","This card is already graded. Choose an ungraded card to postpone.");return;}
    const auto existing=variant(m_selectedVariant);
    if (!existing) {m_queueDirty=true;if (publishQueue(true)) fail("CARD_NOT_FOUND","This review card no longer exists. Continue with the next card.");return;}
    const QDateTime localStart=day.startOfDay(QTimeZone::systemTimeZone());
    if (!localStart.isValid()) {fail("INVALID_POSTPONE_DATE","The selected date has no local start of day. Choose another date.");return;}
    struct model::Variant schedule=*existing;schedule.due=localStart.toUTC().toString(Qt::ISODateWithMs);
    if (!mutate([&]{return upsertVariant(schedule)&&enqueue("variant.upsert",{{"variant",model::toMap(schedule)}});})) {fail("REVIEW_POSTPONE","Could not postpone this card. Check storage and try again.",m_sqlDetail);return;}
    m_queue.removeAt(position);m_sessionOrder.removeAll(m_selectedVariant);advanceAfterRemoval(position);
    if (publishSnapshot()&&publishQueue(true)) done("Review postponed to "+date);
}
void DatabaseWorker::gradeCard(int grade,double recall,double seconds,const QString &expectedVariant)
{
    if (!ready()) return;
    if (grade<0||grade>4||!std::isfinite(recall)||recall<0||recall>1||!std::isfinite(seconds)||seconds<0||seconds>86400) {fail("INVALID_GRADE","Choose a grade from 0 to 4 and a recall fraction between 0 and 1.");return;}
    if (!m_sessionActive||!m_sessionOrder.contains(expectedVariant)||m_selectedVariant!=expectedVariant) {fail("REVIEW_STALE","The review card changed. Read the current card and try again.");return;}
    const auto old=variant(expectedVariant);
    const auto note=old?card(old->cardId):std::nullopt;
    const auto sourceDeck=note?deck(note->deckId):std::nullopt;
    if (!m_sqlDetail.isEmpty()) {fail("REVIEW_LOAD","Could not load this review. Refresh and try again.",m_sqlDetail);return;}
    if (!old||!note||!sourceDeck) {m_queueDirty=true;if (publishQueue(false)) fail("CARD_NOT_FOUND","This review card no longer exists. Choose another card.");return;}
    const bool correction=m_sessionGrades.contains(expectedVariant);
    if (old->reviewCount>=1000000000&&!correction) {fail("REVIEW_LIMIT","This variant has reached the review count limit. Export the collection and inspect its schedule.");return;}
    const struct SessionGrade previous=correction?m_sessionGrades.value(expectedVariant):SessionGrade{*old,{}, {},model::readingBudget(model::question(*note,old->key))};
    const struct model::Variant baseline=previous.baseline;
    const QString reviewedAt=correction?previous.savedReview.reviewedAt:model::nowUtc();
    const double effectiveSeconds=correction?previous.savedReview.responseSeconds:seconds;
    const struct betterflash::scheduler::ReviewResult result=betterflash::scheduler::schedule(baseline.stability,baseline.difficulty,grade,recall,effectiveSeconds,baseline.reviewCount,previous.readingBudget);
    const QString due=QDateTime::fromString(reviewedAt,Qt::ISODateWithMs).addDays(result.intervalDays).toString(Qt::ISODateWithMs);
    const struct model::Variant schedule{old->id,old->cardId,old->key,due,baseline.reviewCount+1,result.stability,result.difficulty};
    const struct model::Review record{correction?previous.savedReview.id:model::uuid(),old->cardId,old->id,correction?previous.savedReview.deckName:sourceDeck->name,grade,recall,effectiveSeconds,reviewedAt,due};
    QVariantMap payload{{"review",model::toMap(record)},{"variant",model::toMap(schedule)}};
    if (correction) {payload.insert("previousReview",model::toMap(previous.savedReview));payload.insert("previousVariant",model::toMap(previous.savedSchedule));}
    bool stale=false;
    if (!mutate([&]{
        const auto persisted=variant(expectedVariant);
        if (!m_sqlDetail.isEmpty()) return false;
        const struct model::Variant expected=correction?previous.savedSchedule:*old;
        if (!persisted||model::toMap(*persisted)!=model::toMap(expected)) {stale=true;return false;}
        if (correction) {
            const auto persistedReview=review(previous.savedReview.id);
            if (!m_sqlDetail.isEmpty()) return false;
            if (!persistedReview||model::toMap(*persistedReview)!=model::toMap(previous.savedReview)) {stale=true;return false;}
            if (!correctReview(record,previous.savedReview)) return false;
        } else if (!insertReview(record)) return false;
        return upsertVariant(schedule)&&enqueue(correction?QStringLiteral("review.correct"):QStringLiteral("review.add"),payload);
    })) {
        if (stale) fail("REVIEW_CONFLICT","This review or its schedule changed on another device. End this review session, sync, and start a new session before grading again.");
        else fail("REVIEW_SAVE","Could not save this review. Check storage and retry the grade.",m_sqlDetail);
        return;
    }
    m_sessionGrades.insert(expectedVariant,SessionGrade{baseline,schedule,record,previous.readingBudget});
    m_queue.removeAll(expectedVariant);m_queueDirty=true;
    emit gradeCommitted(correction);
    if (publishSnapshot()&&publishQueue(false)) done(correction?"Review result updated":"Review saved");
}


bool DatabaseWorker::pendingEntity(const QString &type,const QString &id)
{
    QSqlQuery q(m_db);
    if (type==QStringLiteral("variant")) {
        q.prepare("SELECT 1 FROM local_events WHERE echoed=0 AND ((target_type='variant' AND target_id=?) OR (target_type='card' AND target_id=(SELECT card_id FROM review_variants WHERE id=?))) LIMIT 1");
        q.addBindValue(id);q.addBindValue(id);
    } else {q.prepare("SELECT 1 FROM local_events WHERE echoed=0 AND target_type=? AND target_id=? LIMIT 1");q.addBindValue(type);q.addBindValue(id);}
    if (!q.exec()) {m_sqlDetail=q.lastError().text();return false;}
    return q.next();
}
qint64 DatabaseWorker::entitySequence(const QString &type,const QString &id)
{
    QSqlQuery q(m_db);q.prepare("SELECT seq FROM entity_sequences WHERE entity_type=? AND entity_id=?");q.addBindValue(type);q.addBindValue(id);
    if (!q.exec()) {m_sqlDetail=q.lastError().text();return 0;}
    return q.next()?q.value(0).toLongLong():0;
}
bool DatabaseWorker::recordSequence(const QString &type,const QString &id,qint64 seq)
{ return execute("INSERT INTO entity_sequences(entity_type,entity_id,seq) VALUES(?,?,?) ON CONFLICT(entity_type,entity_id) DO UPDATE SET seq=MAX(seq,excluded.seq)",{type,id,seq}); }
bool DatabaseWorker::eventEffects(const QVariantMap &event,qint64 seq,bool executeEffects,bool &applied)
{
    applied=true;
    const QString type=event.value("type").toString();
    const QVariantMap payload=object(event,"payload");
    if (type==QStringLiteral("deck.delete")) {
        const QString id=payload.value("id").toString();
        return (!executeEffects||eraseDeck(id,seq))&&recordSequence("deck",id,seq);
    }
    if (type==QStringLiteral("card.delete")) {
        const QString id=payload.value("id").toString();
        return (!executeEffects||eraseCard(id,seq))&&recordSequence("card",id,seq);
    }
    if (type==QStringLiteral("deck.upsert")) {
        const struct model::Deck record=*model::deckFromMap(object(payload,"deck"));
        if (!executeEffects) return recordSequence("deck",record.id,seq);
        if (isDeleted("deck",record.id)||seq<=entitySequence("deck",record.id)) return m_sqlDetail.isEmpty();
        if (pendingEntity("deck",record.id)) {applied=false;return m_sqlDetail.isEmpty();}
        if (!record.parentId.isEmpty()&&isDeleted("deck",record.parentId)) return tombstone("deck",record.id,seq)&&recordSequence("deck",record.id,seq);
        if (!validDeckParent(record.id,record.parentId)) {
            if (!record.parentId.isEmpty()&&deck(record.parentId)) {m_sqlDetail="Deck hierarchy contains a cycle.";return false;}
            applied=false;return m_sqlDetail.isEmpty();
        }
        return upsertDeck(record)&&recordSequence("deck",record.id,seq);
    }
    if (type==QStringLiteral("card.upsert")) {
        const struct model::Card record=*model::cardFromMap(object(payload,"card"));
        QList<struct model::Variant> schedules;
        for (const QVariant &value : payload.value("variants").toList()) schedules.append(*model::variantFromMap(value.toMap()));
        if (!executeEffects) {
            if (!recordSequence("card",record.id,seq)) return false;
            for (const struct model::Variant &v : schedules) if (!recordSequence("variant",v.id,seq)) return false;
            return true;
        }
        if (isDeleted("card",record.id)) return m_sqlDetail.isEmpty();
        if (isDeleted("deck",record.deckId)) return tombstone("card",record.id,seq);
        if (pendingEntity("card",record.id)) {applied=false;return m_sqlDetail.isEmpty();}
        if (!deck(record.deckId)) {applied=false;return m_sqlDetail.isEmpty();}
        const bool newer=seq>entitySequence("card",record.id);
        if (newer) {
            if (!upsertCard(record)||!recordSequence("card",record.id,seq)) return false;
            QSet<QString> ids;for (const struct model::Variant &v : schedules) ids.insert(v.id);
            for (const struct model::Variant &v : cardVariants(record.id))
                if (!ids.contains(v.id)&&(!tombstone("variant",v.id,seq)||!execute("DELETE FROM review_variants WHERE id=?",{v.id}))) return false;
        }
        const auto current=card(record.id);
        if (!current) {applied=false;return m_sqlDetail.isEmpty();}
        const QStringList keys=*model::variantKeys(*current);
        for (const struct model::Variant &v : schedules) {
            if (!keys.contains(v.key)||seq<=entitySequence("variant",v.id)) continue;
            if (pendingEntity("variant",v.id)) {applied=false;continue;}
            if (!upsertVariant(v)||!recordSequence("variant",v.id,seq)) return false;
        }
        return m_sqlDetail.isEmpty();
    }
    const bool reviewAdded=type==QStringLiteral("review.add");
    const bool correction=type==QStringLiteral("review.correct");
    const std::optional<struct model::Review> record=(reviewAdded||correction)?std::optional<struct model::Review>(*model::reviewFromMap(object(payload,"review"))):std::nullopt;
    if (reviewAdded&&payload.value("historyOnly").toBool()) {
        if (!executeEffects) return recordSequence("review",record->id,seq);
        if (seq<=entitySequence("review",record->id)) return m_sqlDetail.isEmpty();
        return insertReview(*record)&&recordSequence("review",record->id,seq);
    }
    const struct model::Variant schedule=*model::variantFromMap(object(payload,"variant"));
    if (!executeEffects) return recordSequence("variant",schedule.id,seq)&&(!record||recordSequence("review",record->id,seq));
    if (pendingEntity("card",schedule.cardId)||pendingEntity("variant",schedule.id)) {applied=false;return m_sqlDetail.isEmpty();}
    if (!m_sqlDetail.isEmpty()) return false;
    if (reviewAdded&&seq>entitySequence("review",record->id)) {
        if (!insertReview(*record)||!recordSequence("review",record->id,seq)) return false;
    }
    if (!m_sqlDetail.isEmpty()) return false;
    if (isDeleted("card",schedule.cardId)) return m_sqlDetail.isEmpty();
    if (seq<=entitySequence("variant",schedule.id)) {
        if (!reviewAdded) return m_sqlDetail.isEmpty();
        const auto persisted=variant(schedule.id);
        if (!m_sqlDetail.isEmpty()) return false;
        if (!persisted||schedule.reviewCount<=persisted->reviewCount) return true;
        // A correction must not hide an independently completed later review.
        m_syncReviewConflicts.append(record->id);
    }
    const auto source=card(schedule.cardId);
    if (!source) {applied=false;return m_sqlDetail.isEmpty();}
    if (isDeleted("deck",source->deckId)) return m_sqlDetail.isEmpty();
    if (!model::variantKeys(*source)->contains(schedule.key)) return true;
    if (correction) {
        if (seq<=entitySequence("review",record->id)) return m_sqlDetail.isEmpty();
        const struct model::Review expectedReview=*model::reviewFromMap(object(payload,"previousReview"));
        const struct model::Variant expectedSchedule=*model::variantFromMap(object(payload,"previousVariant"));
        const auto persistedReview=review(record->id);
        const auto persistedSchedule=variant(schedule.id);
        if (!m_sqlDetail.isEmpty()) return false;
        if (!persistedReview||!persistedSchedule||model::toMap(*persistedReview)!=model::toMap(expectedReview)
            ||model::toMap(*persistedSchedule)!=model::toMap(expectedSchedule)) {
            m_syncReviewConflicts.append(record->id);
            return true;
        }
        if (!correctReview(*record,expectedReview)||!recordSequence("review",record->id,seq)) return false;
    }
    return upsertVariant(schedule)&&recordSequence("variant",schedule.id,seq);
}
bool DatabaseWorker::applyRemoteEvent(const QVariantMap &event,qint64 seq,bool &applied)
{
    QSqlQuery seen(m_db);seen.prepare("SELECT seq FROM seen_events WHERE id=?");seen.addBindValue(event.value("id"));
    if (!seen.exec()) {m_sqlDetail=seen.lastError().text();return false;}
    if (seen.next()) {
        const qint64 previous=seen.value(0).toLongLong();
        if (previous>0) {
            applied=true;
            if (previous!=seq) {m_sqlDetail="An event identifier was replayed at a different server sequence.";return false;}
            return true;
        }
        // Local events are already applied. Their echoes establish server order before older deferred events are retried.
        QSqlQuery local(m_db);local.prepare("SELECT event FROM local_events WHERE id=?");local.addBindValue(event.value("id"));
        if (!local.exec()||!local.next()) {m_sqlDetail=local.lastError().text();return false;}
        if (QJsonDocument::fromJson(local.value(0).toString().toUtf8()).object()!=QJsonObject::fromVariantMap(event)) {m_sqlDetail="A local event echo differs from its immutable content.";return false;}
        if (!eventEffects(event,seq,false,applied)) return false;
        return execute("UPDATE seen_events SET seq=? WHERE id=?",{seq,event.value("id")})
            &&execute("UPDATE local_events SET echoed=1 WHERE id=?",{event.value("id")});
    }
    if (!eventEffects(event,seq,true,applied)) return false;
    return !applied||execute("INSERT INTO seen_events(id,seq) VALUES(?,?)",{event.value("id"),seq});
}
bool DatabaseWorker::retryRemoteEvents()
{
    QSqlQuery q(m_db);
    if (!q.exec("SELECT seq,event FROM remote_pending ORDER BY seq")) {m_sqlDetail=q.lastError().text();return false;}
    QList<QPair<qint64,QVariantMap>> events;
    while (q.next()) events.append({q.value(0).toLongLong(),QJsonDocument::fromJson(q.value(1).toString().toUtf8()).object().toVariantMap()});
    QHash<QString,QString> parents;QSqlQuery decks(m_db);
    if (!decks.exec("SELECT id,parent_id FROM decks")) {m_sqlDetail=decks.lastError().text();return false;}
    while (decks.next()) parents.insert(decks.value(0).toString(),decks.value(1).toString());
    for (const auto &entry : events) if (entry.second.value("type")==QStringLiteral("deck.upsert")) {
        const QVariantMap record=object(object(entry.second,"payload"),"deck");parents.insert(record.value("id").toString(),record.value("parentId").toString());
    }
    QHash<QString,int> colors;
    std::function<bool(const QString &)> acyclic=[&](const QString &id) {
        if (id.isEmpty()||!parents.contains(id)) return true;
        if (colors.value(id)==1) return false;if (colors.value(id)==2) return true;colors[id]=1;
        if (!acyclic(parents.value(id))) return false;colors[id]=2;return true;
    };
    for (const QString &id : parents.keys()) if (!acyclic(id)) {m_sqlDetail="Deck hierarchy contains a cycle.";return false;}
    bool progressed=false;
    do {
        progressed=false;
        for (auto it=events.begin();it!=events.end();) {
            bool applied=false;
            if (!applyRemoteEvent(it->second,it->first,applied)) return false;
            if (applied) {
                if (!execute("DELETE FROM remote_pending WHERE seq=?",{it->first})) return false;
                it=events.erase(it);progressed=true;
            } else ++it;
        }
    } while (progressed&&!events.isEmpty());
    return true;
}
void DatabaseWorker::requestSyncBatch(const QString &requestId)
{
    if (!ready()) {emit syncBatchFailed("STORAGE_UNAVAILABLE","Storage is unavailable. Check the data directory and restart.");emit syncBatchFailedForRequest(requestId,"STORAGE_UNAVAILABLE","Storage is unavailable. Check the data directory and restart.");return;}
    const auto reject=[&](const QString &code,const QString &message,const QString &detail={}) {fail(code,message,detail);emit syncBatchFailed(code,message);emit syncBatchFailedForRequest(requestId,code,message);};
    if (requestId.size()>128||requestId.contains(QChar::Null)) {reject("INVALID_SYNC_REQUEST","Choose a sync request identifier of at most 128 characters.");return;}
    QVariantList events;QSqlQuery q(m_db);qint64 bytes=512,mediaBytes=0;bool hasMoreLocal=false;
    QSet<QString> mediaNames;
    const QString mediaDirectory=QDir(QFileInfo(m_path).absolutePath()).filePath("media");
    if (!q.exec("SELECT id,device_id,event_type,payload,created_at FROM sync_outbox ORDER BY position LIMIT 101")) {reject("SYNC_READ","Could not load pending sync changes. Try syncing again.",q.lastError().text());return;}
    while (q.next()) {
        const QJsonDocument payload=QJsonDocument::fromJson(q.value(3).toString().toUtf8());
        if (!payload.isObject()) {reject("SYNC_STORAGE","A stored sync payload is invalid. Preserve the database and inspect details.");return;}
        const QVariantMap event{{"id",q.value(0)},{"deviceId",q.value(1)},{"type",q.value(2)},{"payload",payload.object().toVariantMap()},{"createdAt",q.value(4)}};
        const qsizetype size=json(event).toUtf8().size();
        if (size>model::maxSyncEventBytes) {reject("SYNC_EVENT_LIMIT","A stored change exceeds the sync limit. Shorten the affected card content before syncing.");return;}
        if (events.size()>=100||bytes+size+1>model::maxSyncBatchBytes) {hasMoreLocal=true;break;}
        const auto valid=betterflash::protocol::validateEvent(event);
        if (!valid) {reject("SYNC_STORAGE","A stored sync change is invalid. Preserve the database and inspect details.",valid.error());return;}
        if (event.value("type")==QStringLiteral("card.upsert")) {
            const auto note=model::cardFromMap(object(object(event,"payload"),"card"));
            if (!note) {reject("SYNC_STORAGE","A stored card change is invalid. Preserve the database and inspect details.",note.error());return;}
            const auto names=model::cardMediaReferences(*note);
            if (!names) {reject("SYNC_MEDIA","A stored card has invalid image references. Preserve the database and inspect details.",names.error());return;}
            QSet<QString> combined=mediaNames;
            QStringList additional;
            for (const QString &name:*names) if (!combined.contains(name)) {combined.insert(name);additional.append(name);}
            if (combined.size()>model::maxSyncImages) {hasMoreLocal=true;break;}
            const auto images=model::readBackupMedia(mediaDirectory,additional);
            if (!images) {reject("SYNC_MEDIA","Could not prepare referenced images. Restore the affected images or insert them again before syncing.",images.error());return;}
            qint64 additionalBytes=0;
            for (const struct model::MediaAttachment &image:*images) additionalBytes+=image.bytes.size();
            if (mediaBytes+additionalBytes>model::maxMediaBytes) {hasMoreLocal=true;break;}
            mediaNames=std::move(combined);mediaBytes+=additionalBytes;
        }
        bytes+=size+1;events.append(event);
    }
    QSet<QString> submitted;
    for (const QVariant &event:events) submitted.insert(event.toMap().value("id").toString());
    m_requests.insert(requestId,SyncRequest{submitted,m_cursor});
    m_requestOrder.removeAll(requestId);m_requestOrder.append(requestId);
    while (m_requestOrder.size()>64) m_requests.remove(m_requestOrder.takeFirst());
    emit syncBatchReady({{"deviceId",m_deviceId},{"cursor",m_cursor},{"events",events},{"hasMoreLocal",hasMoreLocal},{"requestId",requestId}});
    done("Sync changes prepared");
}

void DatabaseWorker::applySyncResponse(const QVariantMap &response)
{
    const QString requestId=response.value("requestId").toString();
    const auto appliedSignal=[&](bool success,bool more){emit syncApplied(success,more);emit syncResponseApplied(requestId,success,more);};
    if (!ready()) {appliedSignal(false,false);return;}
    const auto reject=[&](const QString &detail){fail("INVALID_SYNC_RESPONSE","The sync response was rejected. No sync changes were committed.",detail);appliedSignal(false,false);};
    const auto request=m_requests.constFind(requestId);
    if (request==m_requests.cend()||request->cursor!=m_cursor) {reject("Sync request is unknown or its download cursor is no longer current. Prepare a new batch and retry.");return;}
    const auto valid=betterflash::protocol::validateResponse(response,request->cursor,request->submittedIds);
    if (!valid) {reject(valid.error());return;}
    const QVariantList events=response.value("events").toList();
    const qint64 cursor=response.value("cursor").toLongLong();
    QSet<QString> acceptedIds;for (const QVariant &id:response.value("acceptedIds").toList()) acceptedIds.insert(id.toString());
    bool hasPendingOutbox=false;
    m_syncReviewConflicts.clear();
    const bool ok=mutate([&] {
        for (const QVariant &value : events) {
            const QVariantMap wrapper=value.toMap();
            const QVariantMap event=object(wrapper,"event");
            // Retryable dependencies remain durable even while the download cursor advances.
            if (!execute("INSERT INTO remote_pending(seq,event_id,event) VALUES(?,?,?) ON CONFLICT(seq) DO UPDATE SET event=CASE WHEN event_id=excluded.event_id THEN event ELSE NULL END",{wrapper.value("seq"),event.value("id"),json(event)})) return false;
        }
        for (const QString &id : acceptedIds) if (!execute("DELETE FROM sync_outbox WHERE id=?",{id})) return false;
        if (!retryRemoteEvents()||!execute("UPDATE metadata SET value=? WHERE key='sync_cursor'",{QString::number(cursor)})) return false;
        QSqlQuery pending(m_db);
        if (!pending.exec("SELECT EXISTS(SELECT 1 FROM sync_outbox)" )||!pending.next()) {m_sqlDetail=pending.lastError().text();return false;}
        hasPendingOutbox=pending.value(0).toBool();
        return true;
    });
    if (!ok) {fail("SYNC_APPLY","Could not apply sync changes. Existing records and pending uploads were preserved.",m_sqlDetail);appliedSignal(false,false);return;}
    m_cursor=cursor;m_queueDirty=true;
    const bool hasMore=response.value("hasMore").toBool()||hasPendingOutbox;
    if (publishSnapshot()&&publishQueue(false)) {
        if (m_syncReviewConflicts.isEmpty()) done("Sync applied");
        else fail("SYNC_REVIEW_CONFLICT","Sync finished, but a conflicting review correction was not applied. Review the affected cards in History before grading them again.","Review identifiers: "+m_syncReviewConflicts.join(", "));
    }
    appliedSignal(true,hasMore);
}

void DatabaseWorker::exportCollection(const QString &path)
{
    if (!ready()) return;
    if (path.trimmed().isEmpty()||path.size()>4096||path.contains(QChar::Null)||QFileInfo(path).absoluteFilePath()==QFileInfo(m_path).absoluteFilePath()) {fail("INVALID_EXPORT_PATH","Choose a file path different from the application database.");return;}
    QVariantList decks,cards,variants,history,deleted;
    QList<struct model::Card> notes;
    QSqlQuery q(m_db);
    if (!q.exec("SELECT id,name,description,created_at,parent_id FROM decks ORDER BY id")) {fail("EXPORT_READ","Could not read decks for export.",q.lastError().text());return;}
    while (q.next()) decks.append(model::toMap(readDeck(q)));
    if (!q.exec("SELECT id,deck_id,kind,front,back,tags,point_count FROM notes ORDER BY id")) {fail("EXPORT_READ","Could not read cards for export.",q.lastError().text());return;}
    while (q.next()) {const struct model::Card note=readCard(q);notes.append(note);cards.append(model::toMap(note));}
    if (!q.exec("SELECT id,card_id,variant_key,due,review_count,stability,difficulty FROM review_variants ORDER BY id")) {fail("EXPORT_READ","Could not read variants for export.",q.lastError().text());return;}
    while (q.next()) variants.append(model::toMap(readVariant(q)));
    if (!q.exec("SELECT id,card_id,variant_id,deck_name,grade,recall_fraction,response_seconds,reviewed_at,due FROM reviews ORDER BY reviewed_at,id")) {fail("EXPORT_READ","Could not read history for export.",q.lastError().text());return;}
    while (q.next()) history.append(model::toMap(readReview(q)));
    if (!q.exec("SELECT entity_type,entity_id FROM tombstones ORDER BY entity_type,entity_id")) {fail("EXPORT_READ","Could not read retained deletion records.",q.lastError().text());return;}
    while (q.next()) deleted.append(QVariantMap{{"type",q.value(0)},{"id",q.value(1)}});
    const auto references=model::mediaReferences(notes);
    if (!references) {fail("EXPORT_MEDIA","Could not include card images. Repair the media references and export again.",references.error());return;}
    const auto media=model::readBackupMedia(QDir(QFileInfo(m_path).absolutePath()).filePath("media"),*references);
    if (!media) {fail("EXPORT_MEDIA","Could not include card images. Restore the missing or invalid image and export again.",media.error());return;}
    const QJsonObject root{{"version",2},{"media",model::mediaToJson(*media)},{"decks",QJsonArray::fromVariantList(decks)},{"cards",QJsonArray::fromVariantList(cards)},
        {"variants",QJsonArray::fromVariantList(variants)},{"history",QJsonArray::fromVariantList(history)},{"deleted",QJsonArray::fromVariantList(deleted)}};
    QSaveFile file(path);
    const QByteArray bytes=QJsonDocument(root).toJson(QJsonDocument::Indented);
    if (bytes.size()>model::maxBackupBytes) {fail("EXPORT_LIMIT","This collection exceeds the 128 MiB portable backup limit. Export a smaller collection.");return;}
    if (!file.open(QIODevice::WriteOnly)||file.write(bytes)!=bytes.size()||!file.commit()) {fail("EXPORT_WRITE","Could not write the export. Choose a writable file path.",file.errorString());return;}
    done("Collection exported");
}
bool DatabaseWorker::importRecords(const struct model::Collection &records)
{
    QList<struct model::Deck> pendingDecks=records.decks;
    while (!pendingDecks.isEmpty()) {
        bool progressed=false;
        for (auto it=pendingDecks.begin();it!=pendingDecks.end();) {
            const struct model::Deck &record=*it;
            if (!record.parentId.isEmpty()&&!deck(record.parentId)) {++it;continue;}
            if (!deck(record.id)&&(!upsertDeck(record)||!enqueue("deck.upsert",{{"deck",model::toMap(record)}}))) return false;
            it=pendingDecks.erase(it);progressed=true;
        }
        if (!progressed) {m_sqlDetail="Deck hierarchy dependencies could not be resolved.";return false;}
    }
    QSet<QString> addedCards;
    for (const struct model::Card &record : records.cards) {
        if (!card(record.id)) {if (!upsertCard(record)) return false;addedCards.insert(record.id);}
    }
    for (const struct model::Variant &record : records.variants) {
        if (!variant(record.id)&&!upsertVariant(record)) return false;
    }
    for (const struct model::Card &record : records.cards) {
        if (addedCards.contains(record.id)&&!enqueue("card.upsert",cardPayload(record))) return false;
    }
    for (const struct model::Review &record : records.reviews) {
        QSqlQuery existing(m_db);existing.prepare("SELECT 1 FROM reviews WHERE id=?");existing.addBindValue(record.id);
        if (!existing.exec()) {m_sqlDetail=existing.lastError().text();return false;}
        if (!existing.next()) {
            if (!insertReview(record)) return false;
            if (!enqueue("review.add",{{"review",model::toMap(record)},{"historyOnly",true}})) return false;
        }
    }
    for (const struct model::DeletedRecord &record : records.deleted) {
        if (!isDeleted(record.type,record.id)) {
            if (!tombstone(record.type,record.id)) return false;
            if ((record.type==QStringLiteral("deck")||record.type==QStringLiteral("card"))&&!enqueue(record.type+QStringLiteral(".delete"),{{"id",record.id}})) return false;
        }
    }
    return m_sqlDetail.isEmpty();
}
void DatabaseWorker::importCollection(const QString &path)
{
    if (!ready()) return;
    QFile file(path);
    if (path.trimmed().isEmpty()||path.size()>4096||!file.open(QIODevice::ReadOnly)) {fail("IMPORT_READ","Could not open the collection. Choose a readable JSON file.",file.errorString());return;}
    if (file.size()>model::maxBackupBytes) {fail("IMPORT_LIMIT","The collection exceeds the 128 MiB import limit.");return;}
    QJsonParseError parseError;
    const QJsonDocument document=QJsonDocument::fromJson(file.readAll(),&parseError);
    if (!document.isObject()) {fail("INVALID_IMPORT","The collection is not a JSON object. Choose a BetterFlash export.",parseError.errorString());return;}
    const auto validated=model::collectionFromJson(document.object());
    if (!validated) {fail("INVALID_IMPORT","The collection was rejected. No records were changed.",validated.error());return;}
    const struct model::Collection &records=*validated;
    // Import is an additive merge. Existing identifiers must have identical content.
    const auto collision=[&](const QString &detail){fail("IMPORT_COLLISION","An existing identifier has different content. Import into a new collection or resolve the conflicting identifier.",detail);};
    for (const struct model::Deck &record : records.decks) {
        const auto existing=deck(record.id);
        if (isDeleted("deck",record.id)||(existing&&model::toMap(*existing)!=model::toMap(record))) {collision("Deck "+record.id);return;}
    }
    for (const struct model::Card &record : records.cards) {
        const auto existing=card(record.id);
        if (isDeleted("card",record.id)||(existing&&model::toMap(*existing)!=model::toMap(record))) {collision("Card "+record.id);return;}
    }
    for (const struct model::Variant &record : records.variants) {
        const auto existing=variant(record.id);
        if (existing&&model::toMap(*existing)!=model::toMap(record)) {collision("Variant "+record.id);return;}
    }
    for (const struct model::Review &record : records.reviews) {
        QSqlQuery q(m_db);q.prepare("SELECT id,card_id,variant_id,deck_name,grade,recall_fraction,response_seconds,reviewed_at,due FROM reviews WHERE id=?");q.addBindValue(record.id);
        if (!q.exec()) {fail("IMPORT_READ","Could not check existing review history.",q.lastError().text());return;}
        if (q.next()&&model::toMap(readReview(q))!=model::toMap(record)) {collision("Review "+record.id);return;}
    }
    for (const struct model::DeletedRecord &record : records.deleted) {
        if ((record.type==QStringLiteral("deck")&&deck(record.id))||(record.type==QStringLiteral("card")&&card(record.id))||(record.type==QStringLiteral("variant")&&variant(record.id))) {collision("Deletion "+record.id);return;}
    }
    if (!m_sqlDetail.isEmpty()) {fail("IMPORT_READ","Could not validate the destination collection.",m_sqlDetail);return;}
    QHash<QString,QVariantList> importVariants;
    for (const struct model::Variant &schedule:records.variants) importVariants[schedule.cardId].append(model::toMap(schedule));
    for (const struct model::Card &note:records.cards) {
        const QVariantMap envelope{{"id",model::uuid()},{"deviceId",m_deviceId},{"type","card.upsert"},{"payload",QVariantMap{{"card",model::toMap(note)},{"variants",importVariants.value(note.id)}}},{"createdAt",model::nowUtc()}};
        if (json(envelope).toUtf8().size()>model::maxSyncEventBytes) {fail("INVALID_IMPORT","An imported card exceeds the 3 MiB sync event limit. Shorten its content or split it into smaller cards.");return;}
    }
    const auto savedMedia=model::writeBackupMedia(QDir(QFileInfo(m_path).absolutePath()).filePath("media"),records.media);
    if (!savedMedia) {fail("IMPORT_MEDIA","Could not import the images. No collection records changed. Unused verified image files may remain in the media directory.",savedMedia.error());return;}
    if (!mutate([&]{return importRecords(records);})) {fail("IMPORT_ROLLBACK","Import failed. All collection records and pending sync changes were preserved. Unused verified image files may remain in the media directory.",m_sqlDetail);return;}
    m_queueDirty=true;
    if (publishSnapshot()&&publishQueue(false)) done("Collection imported; identical existing records preserved");
}
void DatabaseWorker::loadExample()
{
    if (!ready()) return;
    const struct model::Deck example{model::uuid(),"BetterFlash examples","Markdown, code, mathematics, partial recall, reverse cards, and grouped cloze deletion.",model::nowUtc()};
    QList<struct model::Card> notes;
    const auto add=[&](const QString &kind,const QString &front,const QString &back,const QString &tags,int points=1) {
        notes.append(model::Card{model::uuid(),example.id,kind,front,back,tags,points});
    };
    add("basic","What does `std::expected<T, E>` represent?","Either a value of type `T` or an error of type `E`, represented explicitly in the type.","C++, types");
    add("basic","What keeps a resource's lifetime tied to an object's lifetime?","Resource Acquisition Is Initialization (RAII). The destructor releases the owned resource.","C++, lifetime");
    add("basic","Name three properties of a well-defined transaction.","- Its changes commit together.\n- A failed mutation rolls back.\n- Durable changes survive a restart.","storage, partial recall",3);
    add("basic","What does this C++ function return for a negative input?\n\n```cpp\nstd::expected<int, std::string> twice(const int value) {\n    if (value < 0) return std::unexpected(\"negative\");\n    return value * 2;\n}\n```","An error containing `negative`, rather than an integer.","C++, code");
    add("basic","Write Euler's identity.","$$e^{i\\pi}+1=0$$","mathematics");
    add("reverse","Derivative of $\\sin x$","$\\cos x$","mathematics, reverse");
    add("cloze","{{c1::Atomicity::transaction property}} means a transaction commits together. {{c2::Durability::transaction property}} means it survives restart. A {{c1::rollback}} leaves no partial mutation.","Each distinct cloze group has its own review schedule.","storage, cloze");
    const bool ok=mutate([&] {
        if (!upsertDeck(example)||!enqueue("deck.upsert",{{"deck",model::toMap(example)}})) return false;
        for (const struct model::Card &note : notes) {
            const auto valid=model::cardFromMap(model::toMap(note));assert(valid.has_value());
            if (!upsertCard(note)) return false;
            for (const QString &key : *model::variantKeys(note)) {
                const struct model::Variant schedule{model::variantId(note.id,key),note.id,key,model::nowUtc()};
                if (!upsertVariant(schedule)) return false;
            }
            if (!enqueue("card.upsert",cardPayload(note))) return false;
        }
        return true;
    });
    if (!ok) {fail("EXAMPLE_SAVE","Could not save the example deck. Check storage and try again.",m_sqlDetail);return;}
    if (publishSnapshot()) done("Example deck added");
}
