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
#include <QMap>
#include <QSaveFile>
#include <QScopeGuard>
#include <QSet>
#include <QSqlError>
#include <QSqlQuery>
#include <QThread>
#include <QTimeZone>
#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>

namespace model = betterflash::model;
namespace {
QVariantMap object(const QVariantMap &map, const QString &key) { return map.value(key).toMap(); }
QString json(const QVariantMap &map) { return QString::fromUtf8(QJsonDocument(QJsonObject::fromVariantMap(map)).toJson(QJsonDocument::Compact)); }
struct model::Deck readDeck(const QSqlQuery &q)
{ return {q.value(0).toString(),q.value(1).toString(),q.value(2).toString(),q.value(3).toString(),q.value(4).toString()}; }
struct model::Card readCard(const QSqlQuery &q)
{ return {q.value(0).toString(),q.value(1).toString(),q.value(2).toString(),q.value(3).toString(),q.value(4).toString(),q.value(5).toString(),q.value(6).toInt(),q.value(7).toString().isEmpty()?QVariantMap{}:QJsonDocument::fromJson(q.value(7).toString().toUtf8()).object().toVariantMap(),q.value(8).toString().isEmpty()?QVariantMap{}:QJsonDocument::fromJson(q.value(8).toString().toUtf8()).object().toVariantMap()}; }
struct model::Variant readVariant(const QSqlQuery &q)
{ return {q.value(0).toString(),q.value(1).toString(),q.value(2).toString(),q.value(3).toString(),q.value(4).toInt(),q.value(5).toDouble(),q.value(6).toDouble()}; }
struct model::Review readReview(const QSqlQuery &q)
{ return {q.value(0).toString(),q.value(1).toString(),q.value(2).toString(),q.value(3).toString(),q.value(4).toInt(),q.value(5).toDouble(),q.value(6).toDouble(),q.value(7).toString(),q.value(8).toString(),q.value(9).toString(),q.value(10).toString(),q.value(11).toString()}; }
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
    if (version > 7) { m_sqlDetail = QStringLiteral("Database version is newer than this application.");return false; }
    return mutate([&] {
        const QStringList schema{
            "CREATE TABLE IF NOT EXISTS decks(id TEXT PRIMARY KEY,name TEXT NOT NULL,description TEXT NOT NULL,created_at TEXT NOT NULL,parent_id TEXT NOT NULL DEFAULT '')",
            "CREATE TABLE IF NOT EXISTS notes(id TEXT PRIMARY KEY,deck_id TEXT NOT NULL REFERENCES decks(id) ON DELETE CASCADE,kind TEXT NOT NULL,front TEXT NOT NULL,back TEXT NOT NULL,tags TEXT NOT NULL,point_count INTEGER NOT NULL CHECK(point_count BETWEEN 1 AND 1000),narrations TEXT NOT NULL DEFAULT '{}',narration_fingerprints TEXT NOT NULL DEFAULT '{}')",
            "CREATE TABLE IF NOT EXISTS review_variants(id TEXT PRIMARY KEY,card_id TEXT NOT NULL REFERENCES notes(id) ON DELETE CASCADE,variant_key TEXT NOT NULL,due TEXT NOT NULL,review_count INTEGER NOT NULL CHECK(review_count>=0),stability REAL NOT NULL CHECK(stability BETWEEN .25 AND 3650),difficulty REAL NOT NULL CHECK(difficulty BETWEEN 0 AND 1),UNIQUE(card_id,variant_key))",
            "CREATE INDEX IF NOT EXISTS variants_due ON review_variants(due)",
            "CREATE TABLE IF NOT EXISTS reviews(id TEXT PRIMARY KEY,card_id TEXT NOT NULL,variant_id TEXT NOT NULL,deck_name TEXT NOT NULL,grade INTEGER NOT NULL CHECK(grade BETWEEN 0 AND 4),recall_fraction REAL NOT NULL CHECK(recall_fraction BETWEEN 0 AND 1),response_seconds REAL NOT NULL CHECK(response_seconds BETWEEN 0 AND 86400),reviewed_at TEXT NOT NULL,due TEXT NOT NULL,session_id TEXT NOT NULL DEFAULT '',session_deck_name TEXT NOT NULL DEFAULT '',session_started_at TEXT NOT NULL DEFAULT '')",
            "CREATE INDEX IF NOT EXISTS reviews_time ON reviews(reviewed_at)",
            "CREATE INDEX IF NOT EXISTS reviews_card_time ON reviews(card_id,reviewed_at)",
            "CREATE TABLE IF NOT EXISTS metadata(key TEXT PRIMARY KEY,value TEXT NOT NULL)",
            "CREATE TABLE IF NOT EXISTS sync_outbox(position INTEGER PRIMARY KEY AUTOINCREMENT,id TEXT UNIQUE NOT NULL,device_id TEXT NOT NULL,event_type TEXT NOT NULL,payload TEXT NOT NULL,created_at TEXT NOT NULL,target_type TEXT NOT NULL,target_id TEXT NOT NULL)",
            "CREATE INDEX IF NOT EXISTS outbox_target ON sync_outbox(target_type,target_id)",
            "CREATE TABLE IF NOT EXISTS seen_events(id TEXT PRIMARY KEY,seq INTEGER NOT NULL DEFAULT 0)",
            "CREATE TABLE IF NOT EXISTS local_events(id TEXT PRIMARY KEY,target_type TEXT NOT NULL,target_id TEXT NOT NULL,event TEXT NOT NULL,echoed INTEGER NOT NULL DEFAULT 0)",
            "CREATE INDEX IF NOT EXISTS local_events_target ON local_events(target_type,target_id)",
            "CREATE TABLE IF NOT EXISTS remote_pending(seq INTEGER PRIMARY KEY,event_id TEXT UNIQUE NOT NULL,event TEXT NOT NULL)",
            "CREATE TABLE IF NOT EXISTS entity_sequences(entity_type TEXT NOT NULL,entity_id TEXT NOT NULL,seq INTEGER NOT NULL,PRIMARY KEY(entity_type,entity_id))",
            "CREATE TABLE IF NOT EXISTS tombstones(entity_type TEXT NOT NULL,entity_id TEXT NOT NULL,seq INTEGER NOT NULL,PRIMARY KEY(entity_type,entity_id))",
            "CREATE TABLE IF NOT EXISTS deck_checkpoints(position INTEGER PRIMARY KEY AUTOINCREMENT,id TEXT UNIQUE NOT NULL,deck_id TEXT NOT NULL,created_at TEXT NOT NULL,note TEXT NOT NULL,summary TEXT NOT NULL,snapshot TEXT NOT NULL,changes TEXT NOT NULL)",
            "CREATE INDEX IF NOT EXISTS checkpoints_deck_position ON deck_checkpoints(deck_id,position)",
            "CREATE TABLE IF NOT EXISTS checkpoint_media(name TEXT PRIMARY KEY,bytes BLOB NOT NULL)"
        };
        for (const QString &statement : schema) if (!execute(statement)) return false;
        if (version < 3) {
            QSqlQuery columns(m_db);
            if (!columns.exec("PRAGMA table_info(decks)")) {m_sqlDetail=columns.lastError().text();return false;}
            bool hasParent=false;while (columns.next()) hasParent|=columns.value(1).toString()==QStringLiteral("parent_id");
            if (!hasParent&&!execute("ALTER TABLE decks ADD COLUMN parent_id TEXT NOT NULL DEFAULT ''")) return false;
        }
        if (!execute("CREATE INDEX IF NOT EXISTS decks_parent ON decks(parent_id)")) return false;
        if (version < 4) {
            QSqlQuery columns(m_db);
            if (!columns.exec("PRAGMA table_info(reviews)")) {m_sqlDetail=columns.lastError().text();return false;}
            QSet<QString> names; while (columns.next()) names.insert(columns.value(1).toString());
            if (!names.contains("session_id") && !execute("ALTER TABLE reviews ADD COLUMN session_id TEXT NOT NULL DEFAULT ''")) return false;
            if (!names.contains("session_deck_name") && !execute("ALTER TABLE reviews ADD COLUMN session_deck_name TEXT NOT NULL DEFAULT ''")) return false;
            if (!names.contains("session_started_at") && !execute("ALTER TABLE reviews ADD COLUMN session_started_at TEXT NOT NULL DEFAULT ''")) return false;
            if (!execute("CREATE INDEX IF NOT EXISTS reviews_session ON reviews(session_id,reviewed_at)")) return false;
        }
        if (version < 5) {
            QSqlQuery columns(m_db);
            if (!columns.exec("PRAGMA table_info(notes)")) { m_sqlDetail=columns.lastError().text(); return false; }
            QSet<QString> names; while (columns.next()) names.insert(columns.value(1).toString());
            if (!names.contains("narrations") && !execute("ALTER TABLE notes ADD COLUMN narrations TEXT NOT NULL DEFAULT '{}'")) return false;
        }
        if (version < 6) {
            QSqlQuery columns(m_db);
            if (!columns.exec("PRAGMA table_info(notes)")) { m_sqlDetail=columns.lastError().text(); return false; }
            QSet<QString> names; while (columns.next()) names.insert(columns.value(1).toString());
            if (!names.contains("narration_fingerprints") && !execute("ALTER TABLE notes ADD COLUMN narration_fingerprints TEXT NOT NULL DEFAULT '{}'")) return false;
        }
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
                const struct model::Card note{old.value(0).toString(),old.value(1).toString(),old.value(2).toString(),old.value(3).toString(),old.value(4).toString(),old.value(5).toString(),old.value(6).toInt()};
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
        return execute("PRAGMA user_version=7");
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
    QSqlQuery q(m_db);q.prepare("SELECT id,deck_id,kind,front,back,tags,point_count,narrations,narration_fingerprints FROM notes WHERE id=?");q.addBindValue(id);
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
{ return execute("INSERT INTO notes(id,deck_id,kind,front,back,tags,point_count,narrations,narration_fingerprints) VALUES(?,?,?,?,?,?,?,?,?) ON CONFLICT(id) DO UPDATE SET deck_id=excluded.deck_id,kind=excluded.kind,front=excluded.front,back=excluded.back,tags=excluded.tags,point_count=excluded.point_count,narrations=excluded.narrations,narration_fingerprints=excluded.narration_fingerprints",{r.id,r.deckId,r.kind,r.front,r.back,r.tags,r.pointCount,json(r.narrations),json(r.narrationFingerprints)}); }
bool DatabaseWorker::upsertVariant(const struct model::Variant &r,const QString &reviewId)
{
    const auto previous=variant(r.id);
    if (!m_sqlDetail.isEmpty()) return false;
    const bool unchanged=previous&&model::toMap(*previous)==model::toMap(r);
    if (!execute("INSERT INTO review_variants(id,card_id,variant_key,due,review_count,stability,difficulty) VALUES(?,?,?,?,?,?,?) ON CONFLICT(id) DO UPDATE SET due=excluded.due,review_count=excluded.review_count,stability=excluded.stability,difficulty=excluded.difficulty",{r.id,r.cardId,r.key,r.due,r.reviewCount,r.stability,r.difficulty})
        ||!execute("DELETE FROM tombstones WHERE entity_type='variant' AND entity_id=?",{r.id})) return false;
    if (reviewId.isEmpty()&&unchanged) return true;
    const QString key=QStringLiteral("review_owner:")+r.id;
    return reviewId.isEmpty()?execute("DELETE FROM metadata WHERE key=?",{key})
        :execute("INSERT INTO metadata(key,value) VALUES(?,?) ON CONFLICT(key) DO UPDATE SET value=excluded.value",{key,reviewId});
}
QString DatabaseWorker::reviewOwner(const QString &variantId)
{
    QSqlQuery q(m_db);q.prepare("SELECT value FROM metadata WHERE key=?");q.addBindValue(QStringLiteral("review_owner:")+variantId);
    if (!q.exec()) {m_sqlDetail=q.lastError().text();return {};}
    return q.next()?q.value(0).toString():QString();
}
bool DatabaseWorker::insertReview(const struct model::Review &r)
{
    QSqlQuery existing(m_db);existing.prepare("SELECT id,card_id,variant_id,deck_name,grade,recall_fraction,response_seconds,reviewed_at,due,session_id,session_deck_name,session_started_at FROM reviews WHERE id=?");existing.addBindValue(r.id);
    if (!existing.exec()) {m_sqlDetail=existing.lastError().text();return false;}
    if (existing.next()) {
        if (model::toMap(readReview(existing)) == model::toMap(r)) return true;
        m_sqlDetail="A review identifier already exists with different immutable content.";return false;
    }
    if (!r.sessionId.isEmpty()) {
        QSqlQuery session(m_db);
        session.prepare("SELECT session_deck_name,session_started_at FROM reviews WHERE session_id=? LIMIT 1");
        session.addBindValue(r.sessionId);
        if (!session.exec()) { m_sqlDetail=session.lastError().text(); return false; }
        if (session.next()
            && (session.value(0).toString()!=r.sessionDeckName
                ||session.value(1).toString()!=r.sessionStartedAt)) {
            m_sqlDetail="Review session metadata conflicts with an existing review session.";
            return false;
        }
    }
    return execute("INSERT INTO reviews(id,card_id,variant_id,deck_name,grade,recall_fraction,response_seconds,reviewed_at,due,session_id,session_deck_name,session_started_at) VALUES(?,?,?,?,?,?,?,?,?,?,?,?)",{r.id,r.cardId,r.variantId,r.deckName,r.grade,r.recallFraction,r.responseSeconds,r.reviewedAt,r.due,r.sessionId,r.sessionDeckName,r.sessionStartedAt});
}
std::optional<struct model::Review> DatabaseWorker::review(const QString &id)
{
    QSqlQuery q(m_db);
    q.prepare("SELECT id,card_id,variant_id,deck_name,grade,recall_fraction,response_seconds,reviewed_at,due,session_id,session_deck_name,session_started_at FROM reviews WHERE id=?");
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
        if (!keep.contains(v.id) && (!tombstone("variant",v.id)||!execute("DELETE FROM metadata WHERE key=?",{QStringLiteral("review_owner:")+v.id})||!execute("DELETE FROM review_variants WHERE id=?",{v.id}))) return false;
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
qint64 DatabaseWorker::deletionSequence(const QString &type, const QString &id)
{
    QSqlQuery query(m_db);
    query.prepare("SELECT seq FROM tombstones WHERE entity_type=? AND entity_id=?");
    query.addBindValue(type);
    query.addBindValue(id);
    if (!query.exec()) { m_sqlDetail = query.lastError().text(); return -1; }
    return query.next() ? query.value(0).toLongLong() : -1;
}
bool DatabaseWorker::eraseCard(const QString &id, qint64 seq)
{ return tombstone("card",id,seq)
    &&execute("DELETE FROM metadata WHERE key IN (SELECT 'review_owner:'||id FROM review_variants WHERE card_id=?)",{id})
    &&execute("DELETE FROM notes WHERE id=?",{id}); }
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
    if (!q.exec("SELECT id,deck_id,kind,front,back,tags,point_count,narrations,narration_fingerprints FROM notes ORDER BY id")) {fail("STORAGE_READ","Could not load cards. Try refreshing.",q.lastError().text());return false;}
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
    if (!q.exec("SELECT id,card_id,variant_id,deck_name,grade,recall_fraction,response_seconds,reviewed_at,due,session_id,session_deck_name,session_started_at FROM reviews ORDER BY reviewed_at DESC,id DESC LIMIT 200")) {fail("STORAGE_READ","Could not load review history. Try refreshing.",q.lastError().text());return false;}
    while (q.next()) {
        const struct model::Review review=readReview(q);
        QVariantMap map=model::toMap(review);map.insert("gradeLabel",model::gradeLabel(review.grade));history.append(map);
    }
    const auto sessions=reviewSessions();
    if (!sessions) { fail("STORAGE_READ","Could not load review sessions. Try refreshing.",sessions.error()); return false; }
    emit snapshotReady(decks,cards,history,*sessions);
    return true;
}
std::expected<QVariantList, QString> DatabaseWorker::reviewSessions()
{
    struct Aggregate {
        QString id;
        QString deck;
        QString started;
        QString last;
        bool estimated=false;
        QSet<QString> cards;
        int count=0;
        int successful=0;
        double recall=0;
        double grade=0;
        double seconds=0;
        double best=-1;
        std::array<int,5> outcomes{0,0,0,0,0};
    };
    QSqlQuery q(m_db);
    if (!q.exec("SELECT id,card_id,variant_id,deck_name,grade,recall_fraction,response_seconds,reviewed_at,due,session_id,session_deck_name,session_started_at FROM reviews ORDER BY reviewed_at,id"))
        return std::unexpected(q.lastError().text());
    QHash<QString,struct Aggregate> groups;
    QString previousLegacy;
    QString previousLegacyKey;
    QDateTime previousTime;
    while (q.next()) {
        const struct model::Review review=readReview(q);
        QString key=review.sessionId;
        bool estimated=false;
        const QDateTime at=QDateTime::fromString(review.reviewedAt,Qt::ISODateWithMs).toUTC();
        if (key.isEmpty()) {
            const bool adjacent=!previousLegacy.isEmpty()&&review.deckName==previousLegacy&&previousTime.isValid()&&previousTime.secsTo(at)>=0&&previousTime.secsTo(at)<=1800&&previousTime.date()==at.date();
            if (!adjacent) key=QStringLiteral("legacy-%1").arg(review.id);
            else key=previousLegacyKey;
            estimated=true;
        }
        struct Aggregate &a=groups[key];
        if (a.id.isEmpty()) {
            a.id=key;
            a.deck=review.sessionDeckName.isEmpty()?review.deckName:review.sessionDeckName;
            a.started=review.sessionStartedAt.isEmpty()?review.reviewedAt:review.sessionStartedAt;
            a.estimated=estimated;
        } else if (!review.sessionId.isEmpty()
                   && (a.deck!=(review.sessionDeckName.isEmpty()?review.deckName:review.sessionDeckName)
                       ||a.started!=review.sessionStartedAt)) {
            return std::unexpected(QStringLiteral("Review session metadata is inconsistent for session %1.").arg(key));
        }
        a.last=review.reviewedAt;
        a.cards.insert(review.cardId);
        ++a.count;
        a.successful += review.grade>=2;
        a.recall += review.recallFraction;
        a.grade += review.grade;
        a.seconds += review.responseSeconds;
        if (review.grade>=2&&(a.best<0||review.responseSeconds<a.best)) a.best=review.responseSeconds;
        assert(review.grade>=0&&review.grade<5);
        ++a.outcomes.at(static_cast<size_t>(review.grade));
        if (review.sessionId.isEmpty()) { previousLegacy=review.deckName;previousLegacyKey=key;previousTime=at; } else { previousLegacy.clear();previousLegacyKey.clear();previousTime={}; }
    }
    if (q.lastError().isValid()) return std::unexpected(q.lastError().text());
    QVariantList result;
    for (const struct Aggregate &a:groups) {
        assert(a.count>0);
        QVariantList outcomes;
        for (const int count : a.outcomes) outcomes.append(count);
        result.append(QVariantMap{
            {"id",a.id},{"deckName",a.deck},{"startedAt",a.started},{"lastReviewedAt",a.last},
            {"reviewCount",a.count},{"cardCount",a.cards.size()},
            {"averageRecall",a.recall/a.count},{"averageGrade",a.grade/a.count},
            {"totalResponseSeconds",a.seconds},{"averageResponseSeconds",a.seconds/a.count},
            {"bestSuccessfulSeconds",a.best<0?QVariant{}:QVariant(a.best)},
            {"successfulCount",a.successful},{"outcomes",outcomes},{"estimated",a.estimated}
        });
    }
    std::sort(result.begin(),result.end(),[](const QVariant &left,const QVariant &right){const QVariantMap a=left.toMap(),b=right.toMap();const QString at=a.value("lastReviewedAt").toString(),bt=b.value("lastReviewedAt").toString();return at==bt?a.value("id").toString()>b.value("id").toString():at>bt;});
    return result;
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
        q.prepare("SELECT v.id,v.card_id,v.variant_key,v.due,v.review_count,v.stability,v.difficulty,n.deck_id,n.kind,n.front,n.back,n.tags,n.point_count,n.narrations,n.narration_fingerprints,d.name FROM review_variants v JOIN notes n ON n.id=v.card_id JOIN decks d ON d.id=n.deck_id");
        if (!q.exec()) {fail("REVIEW_LOAD","Could not load the review queue. Refresh and try again.",q.lastError().text());return false;}
        while (q.next()) {
            const struct model::Variant schedule=readVariant(q);
            if (!members.contains(schedule.id)||(!m_sessionGrades.contains(schedule.id)&&schedule.due>now)) continue;
            const struct model::Card note{schedule.cardId,q.value(7).toString(),q.value(8).toString(),q.value(9).toString(),q.value(10).toString(),q.value(11).toString(),q.value(12).toInt(),q.value(13).toString().isEmpty()?QVariantMap{}:QJsonDocument::fromJson(q.value(13).toString().toUtf8()).object().toVariantMap(),q.value(14).toString().isEmpty()?QVariantMap{}:QJsonDocument::fromJson(q.value(14).toString().toUtf8()).object().toVariantMap()};
            const auto keys=model::variantKeys(note);
            if (!keys||!keys->contains(schedule.key)) continue;
            cache.insert(schedule.id,reviewCard(note,schedule,q.value(15).toString()));
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

void DatabaseWorker::advanceReview(const QString &variantId)
{
    if (!m_queue.isEmpty()) {
        const qsizetype current=m_sessionOrder.indexOf(variantId);
        assert(current>=0);
        const QSet<QString> pending(m_queue.cbegin(),m_queue.cend());
        for (qsizetype offset=1;offset<=m_sessionOrder.size();++offset) {
            const QString &candidate=m_sessionOrder.at((current+offset)%m_sessionOrder.size());
            if (pending.contains(candidate)) {m_selectedVariant=candidate;break;}
        }
    }
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
void DatabaseWorker::moveDeck(const QString &id, const QString &parentId)
{
    if (!ready()) { emit moveDeckFinished(false, id, parentId); return; }
    const auto existing = deck(id);
    if (!existing) { fail("DECK_NOT_FOUND", "This deck no longer exists. Refresh the collection.", m_sqlDetail); emit moveDeckFinished(false, id, parentId); return; }
    if (!validDeckParent(id, parentId)) { fail("INVALID_DECK_PARENT", "Choose an existing destination that is not this deck or one of its descendants."); emit moveDeckFinished(false, id, parentId); return; }
    if (existing->parentId == parentId) { done("Deck is already in that location"); emit moveDeckFinished(true, id, parentId); return; }
    const struct model::Deck record{id, existing->name, existing->description, existing->createdAt, parentId};
    if (!mutate([&] { return upsertDeck(record) && enqueue("deck.upsert", {{"deck", model::toMap(record)}}); })) {
        fail("DECK_MOVE", "Could not move the deck. Check storage and try again.", m_sqlDetail); emit moveDeckFinished(false, id, parentId); return;
    }
    m_queueDirty = true;
    if (!publishSnapshot()) { emit moveDeckFinished(true, id, parentId); return; }
    if (!publishQueue(false)) { emit moveDeckFinished(true, id, parentId); return; }
    done("Deck moved");
    emit moveDeckFinished(true, id, parentId);
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
void DatabaseWorker::saveCard(const QString &id,const QString &deckId,const QString &kind,const QString &front,const QString &back,const QString &tags,int points,const QString &requestId)
{
    const QString recordId = id.isEmpty() ? model::uuid() : id;
    if (!ready()) { emit cardSaved(recordId,requestId,false); return; }
    const auto failRequest = [&] (const QString &code, const QString &message, const QString &detail = QString()) {
        fail(code,message,detail); emit cardSaved(recordId,requestId,false);
    };
    const auto previous = id.isEmpty() ? std::optional<struct model::Card>{} : card(id);
    const struct model::Card draft{recordId,deckId,kind,front,back,tags,points,previous?previous->narrations:QVariantMap{},previous?previous->narrationFingerprints:QVariantMap{}};
    QVariantMap narrations;
    QVariantMap fingerprints;
    const auto draftKeys = model::variantKeys(draft);
    if (draftKeys) {
        const QSet<QString> allowedKeys(draftKeys->cbegin(),draftKeys->cend());
        for (auto it = draft.narrations.cbegin(); it != draft.narrations.cend(); ++it)
            if (allowedKeys.contains(it.key().section(QLatin1Char('/'),0,0))) {
                narrations.insert(it.key(),it.value());
                if (draft.narrationFingerprints.contains(it.key())) fingerprints.insert(it.key(),draft.narrationFingerprints.value(it.key()));
            }
    }
    const struct model::Card record{recordId,deckId,kind,front,back,tags,points,narrations,fingerprints};
    const auto valid=model::cardFromMap(model::toMap(record));
    if (!valid) {failRequest(kind==QStringLiteral("cloze")?"INVALID_CLOZE":"INVALID_CARD","Could not save the card. Correct the card fields.",valid.error());return;}
    if (!validateMedia(record)) {failRequest("CARD_MEDIA","Could not save the card images. Restore missing images, insert them again, or split cards containing more than 64 MiB of images.",m_sqlDetail);return;}
    if (!deck(deckId) || isDeleted("deck",deckId)) {failRequest("DECK_NOT_FOUND","Choose an existing deck for this card.",m_sqlDetail);return;}
    if (!id.isEmpty() && (!card(id) || isDeleted("card",id))) {failRequest("CARD_NOT_FOUND","This card no longer exists. Refresh the collection.",m_sqlDetail);return;}
    QList<struct model::Variant> schedules;
    const QList<struct model::Variant> existing=cardVariants(record.id);
    for (const QString &key : *model::variantKeys(record)) {
        const QString variantId=model::variantId(record.id,key);
        const auto found=std::find_if(existing.begin(),existing.end(),[&](const struct model::Variant &v){return v.id==variantId;});
        schedules.append(found==existing.end()?model::Variant{variantId,record.id,key,model::nowUtc()}:*found);
    }
    if (!mutate([&]{return upsertCard(record)&&replaceVariants(record,schedules)&&enqueue("card.upsert",cardPayload(record));})) {failRequest("CARD_SAVE","Could not save the card. Check storage and try again.",m_sqlDetail);return;}
    m_queueDirty=true;
    if (publishSnapshot()&&publishQueue(false)) done("Card saved");
    emit cardSaved(recordId,requestId,true);
}
void DatabaseWorker::setCardNarration(const QString &cardId,const QString &variantKey,const QString &side,const QString &mediaName,const QString &requestId,const QString &fingerprint)
{
    if (!ready()) { emit cardNarrationSaved(cardId,variantKey,side,requestId,false); return; }
    const auto failRequest = [&] (const QString &code, const QString &message, const QString &detail = QString()) {
        fail(code,message,detail); emit cardNarrationSaved(cardId,variantKey,side,requestId,false);
    };
    const auto existing = card(cardId);
    if (!existing || isDeleted("card",cardId)) { failRequest("CARD_NOT_FOUND","This card no longer exists. Refresh the collection."); return; }
    if (side != QStringLiteral("front") && side != QStringLiteral("back")) { failRequest("NARRATION_SIDE_INVALID","Choose the front or back of the card."); return; }
    const auto keys = model::variantKeys(*existing);
    if (!keys || !keys->contains(variantKey)) { failRequest("NARRATION_VARIANT_INVALID","Choose a valid card direction."); return; }
    if (!mediaName.isEmpty()) {
        if (!model::validNarrationMediaName(mediaName)) { failRequest("NARRATION_MEDIA_INVALID","Choose a canonical narration WAV recording."); return; }
        const auto media = model::readBackupMedia(QDir(QFileInfo(m_path).absolutePath()).filePath("media"),{mediaName});
        if (!media) { failRequest("NARRATION_MEDIA_MISSING","The narration recording is unavailable. Save the recording again.",media.error()); return; }
    }
    struct model::Card updated = *existing;
    const QString key = variantKey + QLatin1Char('/') + side;
    if (mediaName.isEmpty()) updated.narrations.remove(key); else updated.narrations.insert(key,mediaName);
    if (mediaName.isEmpty() || fingerprint.isEmpty()) updated.narrationFingerprints.remove(key);
    else updated.narrationFingerprints.insert(key,fingerprint);
    const auto valid = model::cardFromMap(model::toMap(updated));
    if (!valid) { failRequest("NARRATION_INVALID","Could not save this narration.",valid.error()); return; }
    if (!validateMedia(updated)) { failRequest("NARRATION_MEDIA_INVALID","Could not save the narration because an attached media asset is missing or invalid.",m_sqlDetail); return; }
    if (!mutate([&] { return upsertCard(updated) && enqueue("card.upsert",cardPayload(updated)); })) {
        failRequest("NARRATION_SAVE_FAILED","Could not save the narration. Check storage and try again.",m_sqlDetail); return;
    }
    m_queueDirty=true;
    const bool published = publishSnapshot() && publishQueue(false);
    if (published) done("Narration saved");
    emit cardNarrationSaved(cardId,variantKey,side,requestId,true);
}
void DatabaseWorker::confirmCardNarration(const QString &cardId,const QString &variantKey,const QString &side,const QString &mediaName,const QString &expectedFingerprint,const QString &requestId)
{
    if (!ready()) { emit cardNarrationSaved(cardId,variantKey,side,requestId,false); return; }
    QString failureCode = QStringLiteral("NARRATION_CONFIRM_FAILED");
    QString failureMessage = QStringLiteral("Could not confirm the narration. Check storage and try again.");
    const bool committed = mutate([&] {
        const auto existing = card(cardId);
        if (!m_sqlDetail.isEmpty()) return false;
        if (!existing || isDeleted("card",cardId)) {
            failureCode = QStringLiteral("CARD_NOT_FOUND");
            failureMessage = QStringLiteral("This card no longer exists. Refresh the collection.");
            return false;
        }
        if (!m_sqlDetail.isEmpty()) return false;
        const QString key = variantKey + QLatin1Char('/') + side;
        const QString currentFingerprint = model::narrationFingerprint(*existing,variantKey,side);
        if (currentFingerprint.isEmpty() || expectedFingerprint != currentFingerprint
            || existing->narrations.value(key).toString() != mediaName) {
            failureCode = QStringLiteral("NARRATION_TEXT_CHANGED");
            failureMessage = QStringLiteral("The card text or recording changed. Read the current text and listen to the recording before confirming it again.");
            return false;
        }
        struct model::Card updated = *existing;
        updated.narrationFingerprints.insert(key,currentFingerprint);
        const auto valid = model::cardFromMap(model::toMap(updated));
        if (!valid) { m_sqlDetail=valid.error(); return false; }
        if (!validateMedia(updated)) {
            failureCode = QStringLiteral("NARRATION_MEDIA_INVALID");
            failureMessage = QStringLiteral("The narration recording is missing or invalid. Save the recording again.");
            return false;
        }
        return upsertCard(updated) && enqueue("card.upsert",cardPayload(updated));
    });
    if (!committed) {
        fail(failureCode,failureMessage,m_sqlDetail);
        emit cardNarrationSaved(cardId,variantKey,side,requestId,false);
        return;
    }
    m_queueDirty=true;
    const bool published = publishSnapshot() && publishQueue(false);
    if (published) done("Narration confirmed");
    emit cardNarrationSaved(cardId,variantKey,side,requestId,true);
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
void DatabaseWorker::moveCards(const QVariantList &cardIds, const QString &destinationDeckId, const QString &requestId)
{
    if (!ready()) { emit moveCardsFinished(false, destinationDeckId, requestId); return; }
    const auto reject = [&](const QString &code, const QString &message, const QString &detail = QString()) {
        fail(code, message, detail);
        emit moveCardsFinished(false, destinationDeckId, requestId);
    };
    if (!model::validUuid(destinationDeckId)) { reject("INVALID_CARD_MOVE", "Choose a valid destination deck."); return; }
    if (!deck(destinationDeckId)) { reject("DECK_NOT_FOUND", "The destination deck no longer exists. Refresh the collection.", m_sqlDetail); return; }
    if (cardIds.isEmpty() || cardIds.size() > model::maxCollectionRecords) { reject("CARD_MOVE_LIMIT", "Choose between one card and the collection limit."); return; }
    QSet<QString> ids;
    for (const QVariant &value : cardIds) {
        const QString id = value.toString();
        if (!model::validUuid(id)) { reject("INVALID_CARD_ID", "One or more selected cards are invalid."); return; }
        if (ids.contains(id)) { reject("DUPLICATE_CARD_ID", "A card was selected more than once."); return; }
        ids.insert(id);
    }
    QList<struct model::Card> cards;
    for (const QString &id : ids) {
        const auto existing = card(id);
        if (!existing) { reject("CARD_NOT_FOUND", "One or more selected cards no longer exists. Refresh the collection.", m_sqlDetail); return; }
        cards.append(*existing);
    }
    if (std::all_of(cards.cbegin(), cards.cend(), [&](const struct model::Card &record) { return record.deckId == destinationDeckId; })) {
        done("Cards are already in that deck");
        emit moveCardsFinished(true, destinationDeckId, requestId);
        return;
    }
    const bool committed = mutate([&] {
        for (const struct model::Card &existing : cards) {
            if (existing.deckId == destinationDeckId) continue;
            if (!execute("UPDATE notes SET deck_id=? WHERE id=?", {destinationDeckId, existing.id})) return false;
            const struct model::Card moved{existing.id, destinationDeckId, existing.kind, existing.front, existing.back, existing.tags, existing.pointCount, existing.narrations, existing.narrationFingerprints};
            const QVariantMap payload = cardPayload(moved);
            if (!m_sqlDetail.isEmpty() || !enqueue("card.upsert", payload)) return false;
        }
        return true;
    });
    if (!committed) { reject("CARD_MOVE", "Could not move the selected cards. No changes were saved. Check storage and try again.", m_sqlDetail); return; }
    m_queueDirty = true;
    if (!publishSnapshot()) {
        emit moveCardsFinished(true, destinationDeckId, requestId);
        return;
    }
    if (!publishQueue(false)) {
        emit moveCardsFinished(true, destinationDeckId, requestId);
        return;
    }
    done(QStringLiteral("Moved %1 card(s)").arg(cards.size()));
    emit moveCardsFinished(true, destinationDeckId, requestId);
}
void DatabaseWorker::resetReviewState(const QString &id, bool deckScope)
{
    if (!ready()) return;
#ifndef BETTERFLASH_DEVELOPMENT_TOOLS
    Q_UNUSED(id);
    Q_UNUSED(deckScope);
    fail("DEVELOPMENT_TOOLS_DISABLED","Review resets are available only in development builds.");
#else
    if (!model::validUuid(id)) {fail("RESET_TARGET_INVALID","Select a saved deck or card to reset.");return;}
    const bool exists=deckScope?deck(id).has_value():card(id).has_value();
    if (!exists) {fail("RESET_TARGET_MISSING","This deck or card is unavailable. Refresh the collection and try again.",m_sqlDetail);return;}
    int count=0;
    const bool committed=mutate([&] {
        QSqlQuery query(m_db);
        query.prepare(deckScope
            ? "WITH RECURSIVE descendants(id) AS (SELECT id FROM decks WHERE id=? UNION SELECT d.id FROM decks d JOIN descendants ON d.parent_id=descendants.id) SELECT id,deck_id,kind,front,back,tags,point_count,narrations,narration_fingerprints FROM notes WHERE deck_id IN (SELECT id FROM descendants)"
            : "SELECT id,deck_id,kind,front,back,tags,point_count,narrations,narration_fingerprints FROM notes WHERE id=?");
        query.addBindValue(id);
        if (!query.exec()) {m_sqlDetail=query.lastError().text();return false;}
        QList<struct model::Card> cards;
        while (query.next()) cards.append(readCard(query));
        if (query.lastError().isValid()) {m_sqlDetail=query.lastError().text();return false;}
        query.finish();
        const QString due=model::nowUtc();
        for (const struct model::Card &note:cards) {
            const auto keys=model::variantKeys(note);
            if (!keys) {m_sqlDetail=keys.error();return false;}
            assert(!keys->isEmpty());
            for (const QString &key:*keys) {
                const struct model::Variant schedule{model::variantId(note.id,key),note.id,key,due};
                if (!upsertVariant(schedule)) return false;
                // A reset detaches the schedule from any previous grade correction.
                if (!execute("DELETE FROM metadata WHERE key=?",{QStringLiteral("review_owner:")+schedule.id})) return false;
            }
            if (!enqueue("card.upsert",cardPayload(note))) return false;
        }
        count=cards.size();
        return m_sqlDetail.isEmpty();
    });
    if (!committed) {fail("REVIEW_RESET_FAILED","Could not reset review state. Check storage and try again.",m_sqlDetail);return;}
    m_queue.clear();m_sessionOrder.clear();m_queueCache.clear();m_sessionGrades.clear();
    m_selectedVariant.clear();m_sessionTotal=0;m_sessionActive=false;m_queueDirty=true;
    // End the session only after the reset is durable, even if refreshing later fails.
    emit reviewQueueReady({},0,true,{},false,{});
    if (publishSnapshot()) done(QStringLiteral("Reset %1 source card(s). All variants are new and due now.").arg(count));
#endif
}
void DatabaseWorker::beginReview(const QString &deckId)
{
    if (!ready()) return;
    if (!deckId.isEmpty() && (!model::validUuid(deckId)||!deck(deckId))) {fail("DECK_NOT_FOUND","Choose an existing deck to review.");return;}
    const QString sessionId=model::uuid();
    const QString sessionStartedAt=model::nowUtc();
    QString sessionDeckName=QStringLiteral("All decks");
    if (!deckId.isEmpty()) {
        const auto scoped=deck(deckId);
        if (scoped) sessionDeckName=scoped->name;
    }
    QSqlQuery q(m_db);
    q.prepare("SELECT v.id FROM review_variants v JOIN notes n ON n.id=v.card_id WHERE v.due<=?"+QString(deckId.isEmpty()?"":" AND n.deck_id IN (WITH RECURSIVE descendants(id) AS (SELECT id FROM decks WHERE id=? UNION ALL SELECT d.id FROM decks d JOIN descendants ON d.parent_id=descendants.id) SELECT id FROM descendants)")+" ORDER BY v.due,n.id,v.variant_key");
    q.addBindValue(model::nowUtc());if (!deckId.isEmpty()) q.addBindValue(deckId);
    if (!q.exec()) {fail("REVIEW_START","Could not start review. Refresh and try again.",q.lastError().text());return;}
    m_sessionId=sessionId;m_sessionStartedAt=sessionStartedAt;m_sessionDeckName=sessionDeckName;
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
    if (correction&&previous.savedReview.grade==grade&&qFuzzyCompare(previous.savedReview.recallFraction+1.0,recall+1.0)) {
        advanceReview(expectedVariant);
        if (publishQueue(false)) done("Review card selected");
        return;
    }
    const struct model::Variant baseline=previous.baseline;
    const QString reviewedAt=correction?previous.savedReview.reviewedAt:model::nowUtc();
    const double effectiveSeconds=correction?previous.savedReview.responseSeconds:seconds;
    const struct betterflash::scheduler::ReviewResult result=betterflash::scheduler::schedule(baseline.stability,baseline.difficulty,grade,recall,effectiveSeconds,baseline.reviewCount,previous.readingBudget);
    const QString due=QDateTime::fromString(reviewedAt,Qt::ISODateWithMs).addDays(result.intervalDays).toString(Qt::ISODateWithMs);
    const struct model::Variant schedule{old->id,old->cardId,old->key,due,baseline.reviewCount+1,result.stability,result.difficulty};
    const struct model::Review record{correction?previous.savedReview.id:model::uuid(),old->cardId,old->id,correction?previous.savedReview.deckName:sourceDeck->name,grade,recall,effectiveSeconds,reviewedAt,due,correction?previous.savedReview.sessionId:m_sessionId,correction?previous.savedReview.sessionDeckName:m_sessionDeckName,correction?previous.savedReview.sessionStartedAt:m_sessionStartedAt};
    QVariantMap payload{{"review",model::toMap(record)},{"variant",model::toMap(schedule)}};
    if (correction) {payload.insert("previousReview",model::toMap(previous.savedReview));payload.insert("previousVariant",model::toMap(previous.savedSchedule));}
    bool stale=false;
    if (!mutate([&]{
        const auto persisted=variant(expectedVariant);
        if (!m_sqlDetail.isEmpty()) return false;
        const struct model::Variant expected=correction?previous.savedSchedule:*old;
        if (!persisted||model::toMap(*persisted)!=model::toMap(expected)) {stale=true;return false;}
        if (correction) {
            if (reviewOwner(expectedVariant)!=previous.savedReview.id) {stale=m_sqlDetail.isEmpty();return false;}
            const auto persistedReview=review(previous.savedReview.id);
            if (!m_sqlDetail.isEmpty()) return false;
            if (!persistedReview||model::toMap(*persistedReview)!=model::toMap(previous.savedReview)) {stale=true;return false;}
            if (!correctReview(record,previous.savedReview)) return false;
        } else if (!insertReview(record)) return false;
        return upsertVariant(schedule,record.id)&&enqueue(correction?QStringLiteral("review.correct"):QStringLiteral("review.add"),payload);
    })) {
        if (stale) fail("REVIEW_CONFLICT","This review or its schedule changed. End this review session, sync, and start a new session before grading again.");
        else fail("REVIEW_SAVE","Could not save this review. Check storage and retry the grade.",m_sqlDetail);
        return;
    }
    m_sessionGrades.insert(expectedVariant,SessionGrade{baseline,schedule,record,previous.readingBudget});
    m_queue.removeAll(expectedVariant);m_queueDirty=true;
    advanceReview(expectedVariant);
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
bool DatabaseWorker::pendingRestore(const QString &type,const QString &id,bool authoritativeOnly)
{
    QSqlQuery query(m_db);
    query.prepare("SELECT 1 FROM local_events WHERE echoed=0 AND target_type=? AND target_id=? AND json_extract(event,'$.payload.restore')=1"
                  + QString(authoritativeOnly ? " AND COALESCE(json_extract(event,'$.payload.restoreDependency'),0)=0" : "") + " LIMIT 1");
    query.addBindValue(type);
    query.addBindValue(id);
    if (!query.exec()) { m_sqlDetail=query.lastError().text(); return false; }
    return query.next();
}
bool DatabaseWorker::applyDeckDelete(const QString &id,qint64 seq,bool &applied)
{
    QSqlQuery hierarchy(m_db);
    hierarchy.prepare("WITH RECURSIVE subtree(id) AS (SELECT id FROM decks WHERE id=? UNION SELECT d.id FROM decks d JOIN subtree s ON d.parent_id=s.id) SELECT d.id,d.parent_id FROM decks d JOIN subtree s ON s.id=d.id");
    hierarchy.addBindValue(id);
    if (!hierarchy.exec()) { m_sqlDetail=hierarchy.lastError().text(); return false; }
    QHash<QString,QString> parents;
    while (hierarchy.next()) {
        if (parents.size() >= model::maxCollectionRecords) { m_sqlDetail="The deleted deck subtree exceeds its record limit."; return false; }
        parents.insert(hierarchy.value(0).toString(),hierarchy.value(1).toString());
    }
    if (hierarchy.lastError().isValid()) { m_sqlDetail=hierarchy.lastError().text(); return false; }
    if (parents.isEmpty()) {
        if (pendingRestore("deck",id)) { applied=false; return m_sqlDetail.isEmpty(); }
        if (seq < std::max(entitySequence("restore-deck",id),entitySequence("restore-container",id))) return m_sqlDetail.isEmpty();
        return tombstone("deck",id,seq);
    }
    QHash<QString,int> depths;
    for (auto it=parents.cbegin();it!=parents.cend();++it) {
        QString current=it.key();
        QStringList path;
        QSet<QString> seen;
        while (parents.contains(current)&&!depths.contains(current)) {
            if (seen.contains(current)) { m_sqlDetail="The deck hierarchy contains a cycle."; return false; }
            seen.insert(current);path.append(current);current=parents.value(current);
        }
        int depth=depths.value(current);
        for (auto reverse=path.crbegin();reverse!=path.crend();++reverse) depths.insert(*reverse,++depth);
    }
    QStringList ordered=parents.keys();
    std::sort(ordered.begin(),ordered.end(),[&](const QString &left,const QString &right){const int a=depths.value(left),b=depths.value(right);return a==b?left<right:a<b;});
    QSet<QString> protectedDecks;
    QSet<QString> protectedScopes;
    QSet<QString> pendingScopes;
    for (const QString &deckId:ordered) {
        const bool pendingContainer=pendingRestore("deck",deckId);
        const bool pendingScope=pendingRestore("deck",deckId,true);
        const bool inherited=protectedScopes.contains(parents.value(deckId));
        const bool inheritedPending=pendingScopes.contains(parents.value(deckId));
        const bool scope=inherited||pendingScope||seq<entitySequence("restore-deck",deckId);
        const bool container=pendingContainer||seq<entitySequence("restore-container",deckId);
        if (!m_sqlDetail.isEmpty()) return false;
        if (scope) protectedScopes.insert(deckId);
        if (pendingScope||inheritedPending) pendingScopes.insert(deckId);
        if (scope||container) protectedDecks.insert(deckId);
        if (pendingContainer||pendingScope||inheritedPending) applied=false;
    }
    struct DeletedCard { QString id{}; QString deckId{}; bool protect=false; };
    QList<struct DeletedCard> cards;
    for (const QString &deckId:ordered) {
        QSqlQuery query(m_db);query.prepare("SELECT id FROM notes WHERE deck_id=?");query.addBindValue(deckId);
        if (!query.exec()) { m_sqlDetail=query.lastError().text(); return false; }
        while (query.next()) {
            if (cards.size() >= model::maxCollectionRecords) { m_sqlDetail="The deleted deck's cards exceed their record limit."; return false; }
            const QString cardId=query.value(0).toString();
            const bool pending=pendingRestore("card",cardId);
            const bool protect=protectedScopes.contains(deckId)||pending||seq<entitySequence("restore-card",cardId);
            if (!m_sqlDetail.isEmpty()) return false;
            if (pending||pendingScopes.contains(deckId)) applied=false;
            if (protect&&!protectedDecks.contains(deckId)) {
                m_sqlDetail="A restored card was moved into a concurrently deleted deck. Move it back to the restored deck before syncing again.";return false;
            }
            cards.append(DeletedCard{cardId,deckId,protect});
        }
        if (query.lastError().isValid()) { m_sqlDetail=query.lastError().text(); return false; }
    }
    for (const QString &deckId:protectedDecks) {
        const QString parent=parents.value(deckId);
        if (parents.contains(parent)&&!protectedDecks.contains(parent)) {
            m_sqlDetail="A restored deck was moved into a concurrently deleted parent. Move it back to its restored parent before syncing again.";return false;
        }
    }
    for (const struct DeletedCard &record:cards)
        if (!record.protect&&!eraseCard(record.id,seq)) return false;
    for (auto it=ordered.crbegin();it!=ordered.crend();++it)
        if (!protectedDecks.contains(*it)
            &&(!tombstone("deck",*it,seq)||!execute("DELETE FROM decks WHERE id=?",{*it}))) return false;
    return true;
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
        return (!executeEffects||applyDeckDelete(id,seq,applied))
            && (!applied || recordSequence("deck",id,seq));
    }
    if (type==QStringLiteral("card.delete")) {
        const QString id=payload.value("id").toString();
        if (executeEffects) {
            if (seq < entitySequence("restore-card",id)) return m_sqlDetail.isEmpty();
            if (pendingRestore("card",id)) { applied=false; return m_sqlDetail.isEmpty(); }
        }
        return (!executeEffects||eraseCard(id,seq))&&recordSequence("card",id,seq);
    }
    if (type==QStringLiteral("deck.upsert")) {
        const struct model::Deck record=*model::deckFromMap(object(payload,"deck"));
        const bool restore = payload.value("restore").toBool();
        const bool dependency = payload.value("restoreDependency").toBool();
        if (!executeEffects) {
            if (restore && !recordSequence(dependency ? "restore-container" : "restore-deck",record.id,seq)) return false;
            return dependency || recordSequence("deck",record.id,seq);
        }
        if (dependency) {
            const auto existing = deck(record.id);
            if (!m_sqlDetail.isEmpty()) return false;
            if (existing) return recordSequence("restore-container",record.id,seq);
        }
        const qint64 deletedAt = deletionSequence("deck",record.id);
        const qint64 recordedAt = entitySequence("deck",record.id);
        if (!m_sqlDetail.isEmpty()) return false;
        if ((deletedAt >= 0 && !restore) || seq <= recordedAt || (restore && seq <= deletedAt)) return true;
        if (pendingEntity("deck",record.id)) {applied=false;return m_sqlDetail.isEmpty();}
        if (!record.parentId.isEmpty()&&isDeleted("deck",record.parentId)) {
            if (restore) { applied=false; return m_sqlDetail.isEmpty(); }
            return tombstone("deck",record.id,seq)&&recordSequence("deck",record.id,seq);
        }
        if (!validDeckParent(record.id,record.parentId)) {
            if (!record.parentId.isEmpty()&&deck(record.parentId)) {m_sqlDetail="Deck hierarchy contains a cycle.";return false;}
            applied=false;return m_sqlDetail.isEmpty();
        }
        return upsertDeck(record)
            && (!restore || execute("DELETE FROM tombstones WHERE entity_type='deck' AND entity_id=?",{record.id}))
            && (!restore || recordSequence(dependency ? "restore-container" : "restore-deck",record.id,seq))
            && recordSequence("deck",record.id,seq);
    }
    if (type==QStringLiteral("card.upsert")) {
        const QVariantMap incomingCardMap = object(payload,"card");
        const auto parsedRecord = model::cardFromMap(incomingCardMap);
        if (!parsedRecord) { m_sqlDetail=parsedRecord.error(); return false; }
        struct model::Card record=*parsedRecord;
        const auto stored = (!incomingCardMap.contains(QStringLiteral("narrations"))
            || !incomingCardMap.contains(QStringLiteral("narrationFingerprints"))) ? card(record.id) : std::optional<struct model::Card>{};
        if (!m_sqlDetail.isEmpty()) return false;
        if (!incomingCardMap.contains(QStringLiteral("narrations"))) {
            if (stored) {
                const auto keys=model::variantKeys(record);
                if (!keys) { m_sqlDetail=keys.error(); return false; }
                const QSet<QString> allowed(keys->cbegin(),keys->cend());
                for (auto it=stored->narrations.cbegin();it!=stored->narrations.cend();++it) {
                    const QString variantKey=it.key().section(QLatin1Char('/'),0,0);
                    if (allowed.contains(variantKey)) record.narrations.insert(it.key(),it.value());
                }
            }
        }
        if (!incomingCardMap.contains(QStringLiteral("narrationFingerprints")) && stored) {
            for (auto it=stored->narrationFingerprints.cbegin();it!=stored->narrationFingerprints.cend();++it) {
                if (record.narrations.contains(it.key())
                    && record.narrations.value(it.key()) == stored->narrations.value(it.key()))
                    record.narrationFingerprints.insert(it.key(),it.value());
            }
        }
        QList<struct model::Variant> schedules;
        for (const QVariant &value : payload.value("variants").toList()) schedules.append(*model::variantFromMap(value.toMap()));
        const bool restore = payload.value("restore").toBool();
        if (!executeEffects) {
            if (!recordSequence("card",record.id,seq)) return false;
            if (restore) {
                if (!recordSequence("restore-card",record.id,seq)) return false;
                for (const QVariant &id : payload.value("freshVariantIds").toList())
                    if (!recordSequence("variant",id.toString(),seq)) return false;
            } else {
                for (const struct model::Variant &v : schedules) if (!recordSequence("variant",v.id,seq)) return false;
            }
            return true;
        }
        const qint64 deletedAt = deletionSequence("card",record.id);
        if (!m_sqlDetail.isEmpty()) return false;
        if ((deletedAt >= 0 && !restore) || (restore && seq <= deletedAt)) return true;
        if (restore && seq < entitySequence("card",record.id)) return m_sqlDetail.isEmpty();
        if (pendingEntity("card",record.id)) {applied=false;return m_sqlDetail.isEmpty();}
        if (isDeleted("deck",record.deckId)) {
            if (restore) { applied=false; return m_sqlDetail.isEmpty(); }
            return tombstone("card",record.id,seq);
        }
        if (!deck(record.deckId)) {applied=false;return m_sqlDetail.isEmpty();}
        const bool newer=seq>entitySequence("card",record.id);
        if (newer) {
            if (!upsertCard(record)
                || (restore && !execute("DELETE FROM tombstones WHERE entity_type='card' AND entity_id=?",{record.id}))
                || (restore && !recordSequence("restore-card",record.id,seq))
                || !recordSequence("card",record.id,seq)) return false;
            QSet<QString> ids;for (const struct model::Variant &v : schedules) ids.insert(v.id);
            for (const struct model::Variant &v : cardVariants(record.id))
                if (!ids.contains(v.id)&&(!tombstone("variant",v.id,seq)||!execute("DELETE FROM metadata WHERE key=?",{QStringLiteral("review_owner:")+v.id})||!execute("DELETE FROM review_variants WHERE id=?",{v.id}))) return false;
        }
        const auto current=card(record.id);
        if (!current) {applied=false;return m_sqlDetail.isEmpty();}
        const QStringList keys=*model::variantKeys(*current);
        QSet<QString> freshVariantIds;
        if (restore)
            for (const QVariant &id : payload.value("freshVariantIds").toList()) freshVariantIds.insert(id.toString());
        for (const struct model::Variant &v : schedules) {
            if (!keys.contains(v.key)) continue;
            if (restore) {
                const auto retained = variant(v.id);
                if (!m_sqlDetail.isEmpty()) return false;
                const bool fresh = freshVariantIds.contains(v.id);
                if (retained && !fresh) continue;
                if (fresh && seq <= entitySequence("variant",v.id)) continue;
                if (pendingEntity("variant",v.id)) { applied=false; continue; }
                if (!upsertVariant(v)||!recordSequence("variant",v.id,seq)) return false;
                continue;
            }
            if (seq<=entitySequence("variant",v.id)) continue;
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
    if (!executeEffects) return recordSequence("variant",schedule.id,seq)
        &&(!record||recordSequence("review",record->id,seq))&&(!reviewAdded||recordSequence("review-start",record->id,seq));
    if (pendingEntity("card",schedule.cardId)||pendingEntity("variant",schedule.id)) {applied=false;return m_sqlDetail.isEmpty();}
    if (!m_sqlDetail.isEmpty()) return false;
    if (reviewAdded) {
        if (seq>entitySequence("review",record->id)&&(!insertReview(*record)||!recordSequence("review",record->id,seq))) return false;
        if (!recordSequence("review-start",record->id,seq)) return false;
    }
    if (!m_sqlDetail.isEmpty()) return false;
    if (correction) {
        if (seq<=entitySequence("review",record->id)) return m_sqlDetail.isEmpty();
        const auto persistedReview=review(record->id);
        const bool deleted=isDeleted("card",schedule.cardId);
        const auto source=card(schedule.cardId);
        if (!m_sqlDetail.isEmpty()) return false;
        if (!deleted&&!source) {applied=false;return true;}
        if (!persistedReview||persistedReview->cardId!=record->cardId||persistedReview->variantId!=record->variantId
            ||persistedReview->reviewedAt!=record->reviewedAt||persistedReview->responseSeconds!=record->responseSeconds||persistedReview->deckName!=record->deckName
            ||persistedReview->sessionId!=record->sessionId||persistedReview->sessionDeckName!=record->sessionDeckName||persistedReview->sessionStartedAt!=record->sessionStartedAt) {
            m_syncReviewConflicts.append(record->id);
            return true;
        }
        // Review results use server order while identity and timing remain immutable.
        if (!correctReview(*record,*persistedReview)||!recordSequence("review",record->id,seq)) return false;
    }
    if (isDeleted("card",schedule.cardId)) return m_sqlDetail.isEmpty();
    const auto source=card(schedule.cardId);
    if (!source) {applied=false;return m_sqlDetail.isEmpty();}
    if (isDeleted("deck",source->deckId)) return m_sqlDetail.isEmpty();
    if (!model::variantKeys(*source)->contains(schedule.key)) return true;
    const auto persistedSchedule=variant(schedule.id);
    const QString owner=reviewOwner(schedule.id);
    const qint64 variantSequence=entitySequence("variant",schedule.id);
    if (!m_sqlDetail.isEmpty()) return false;
    if (correction) {
        const struct model::Variant previousSchedule=*model::variantFromMap(object(payload,"previousVariant"));
        const bool matchesUnownedSchedule=owner.isEmpty()&&persistedSchedule
            &&model::toMap(*persistedSchedule)==model::toMap(previousSchedule);
        if (!persistedSchedule||(owner!=record->id&&!matchesUnownedSchedule)||schedule.reviewCount!=persistedSchedule->reviewCount||seq<=variantSequence) {
            m_syncReviewConflicts.append(record->id);
            return true;
        }
    } else if (reviewAdded) {
        if (persistedSchedule&&schedule.reviewCount<persistedSchedule->reviewCount) return true;
        if (seq<=variantSequence) {
            // Independent reviews retain their order even after a correction echo.
            const bool laterReview=persistedSchedule&&!owner.isEmpty()
                &&(schedule.reviewCount>persistedSchedule->reviewCount||seq>entitySequence("review-start",owner));
            if (!m_sqlDetail.isEmpty()) return false;
            if (!laterReview) return true;
            m_syncReviewConflicts.append(record->id);
        }
    } else if (seq<=variantSequence) return true;
    return upsertVariant(schedule,record?record->id:QString())&&recordSequence("variant",schedule.id,seq);
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
        // Local events are already applied. Their echoes establish server order
        // and restore existence barriers before deferred remote changes retry.
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
        const QVariantMap payload=object(entry.second,"payload");
        const QVariantMap record=object(payload,"deck");
        if (payload.value("restoreDependency").toBool() && parents.contains(record.value("id").toString())) continue;
        parents.insert(record.value("id").toString(),record.value("parentId").toString());
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
        else fail("SYNC_REVIEW_CONFLICT","Sync finished with conflicting review schedules. The latest review results are in History; a newer or explicitly changed schedule was preserved. End this review session before grading these cards again.","Review identifiers: "+m_syncReviewConflicts.join(", "));
    }
    appliedSignal(true,hasMore);
}

void DatabaseWorker::exportCollection(const QString &path)
{
    if (!ready()) return;
    if (path.trimmed().isEmpty()||path.size()>4096||path.contains(QChar::Null)||QFileInfo(path).absoluteFilePath()==QFileInfo(m_path).absoluteFilePath()) {fail("INVALID_EXPORT_PATH","Choose a file path different from the application database.");return;}
    QVariantList decks,cards,variants,history,deleted,checkpoints;
    QList<struct model::Card> notes;
    QSqlQuery q(m_db);
    if (!q.exec("SELECT id,name,description,created_at,parent_id FROM decks ORDER BY id")) {fail("EXPORT_READ","Could not read decks for export.",q.lastError().text());return;}
    while (q.next()) decks.append(model::toMap(readDeck(q)));
    if (!q.exec("SELECT id,deck_id,kind,front,back,tags,point_count,narrations,narration_fingerprints FROM notes ORDER BY id")) {fail("EXPORT_READ","Could not read cards for export.",q.lastError().text());return;}
    while (q.next()) {const struct model::Card note=readCard(q);notes.append(note);cards.append(model::toMap(note));}
    if (!q.exec("SELECT id,card_id,variant_key,due,review_count,stability,difficulty FROM review_variants ORDER BY id")) {fail("EXPORT_READ","Could not read variants for export.",q.lastError().text());return;}
    while (q.next()) variants.append(model::toMap(readVariant(q)));
    if (!q.exec("SELECT id,card_id,variant_id,deck_name,grade,recall_fraction,response_seconds,reviewed_at,due,session_id,session_deck_name,session_started_at FROM reviews ORDER BY reviewed_at,id")) {fail("EXPORT_READ","Could not read history for export.",q.lastError().text());return;}
    while (q.next()) history.append(model::toMap(readReview(q)));
    if (!q.exec("SELECT entity_type,entity_id FROM tombstones ORDER BY entity_type,entity_id")) {fail("EXPORT_READ","Could not read retained deletion records.",q.lastError().text());return;}
    while (q.next()) deleted.append(QVariantMap{{"type",q.value(0)},{"id",q.value(1)}});
    if (!q.exec("SELECT id,deck_id,created_at,note,summary,snapshot,changes FROM deck_checkpoints ORDER BY position")) {fail("EXPORT_READ","Could not read deck checkpoints for export.",q.lastError().text());return;}
    while (q.next()) {
        const QJsonDocument snapshot = QJsonDocument::fromJson(q.value(5).toString().toUtf8());
        const QJsonDocument changes = QJsonDocument::fromJson(q.value(6).toString().toUtf8());
        if (!snapshot.isObject() || !changes.isObject()) {fail("EXPORT_CHECKPOINT","A stored checkpoint is damaged. Restore the database from a known backup before exporting.");return;}
        const auto checkpoint = model::checkpointFromMap({{"id",q.value(0)},{"deckId",q.value(1)},{"createdAt",q.value(2)},
            {"note",q.value(3)},{"summary",q.value(4)},{"snapshot",snapshot.object().toVariantMap()},{"changes",changes.object().toVariantMap()}});
        if (!checkpoint) {fail("EXPORT_CHECKPOINT","A stored checkpoint is invalid. Restore the database from a known backup before exporting.",checkpoint.error());return;}
        checkpoints.append(*checkpoint);
        for (const QVariant &value : checkpoint->value("snapshot").toMap().value("cards").toList()) {
            const auto note = model::cardFromMap(value.toMap());
            assert(note.has_value());
            notes.append(*note);
        }
    }
    const auto references=model::mediaReferences(notes);
    if (!references) {fail("EXPORT_MEDIA","Could not include card images or recordings. Repair the media references and export again.",references.error());return;}
    QList<struct model::MediaAttachment> media;
    qint64 mediaBytes = 0;
    const QString mediaDirectory = QDir(QFileInfo(m_path).absolutePath()).filePath("media");
    for (const QString &name : *references) {
        QSqlQuery cached(m_db);
        cached.prepare("SELECT bytes FROM checkpoint_media WHERE name=?");
        cached.addBindValue(name);
        if (!cached.exec()) {fail("EXPORT_READ","Could not read preserved checkpoint media.",cached.lastError().text());return;}
        if (cached.next()) {
            const QByteArray bytes = cached.value(0).toByteArray();
            const auto valid = model::validateMediaAttachment(name,bytes);
            if (!valid) {fail("EXPORT_MEDIA","Preserved checkpoint media is damaged. Restore the database from a known backup before exporting.",valid.error());return;}
            media.append(model::MediaAttachment{name,bytes});
        } else {
            const auto attachment = model::readBackupMedia(mediaDirectory,{name});
            if (!attachment) {fail("EXPORT_MEDIA","Could not include card images or recordings. Restore the missing or invalid media and export again.",attachment.error());return;}
            assert(attachment->size() == 1);
            media.append(attachment->first());
        }
        mediaBytes += media.last().bytes.size();
        if (mediaBytes > model::maxMediaBytes) {fail("EXPORT_LIMIT","The collection and its checkpoints exceed the 64 MiB portable media limit.");return;}
    }
    const QJsonObject root{{"version",5},{"media",model::mediaToJson(media)},{"decks",QJsonArray::fromVariantList(decks)},{"cards",QJsonArray::fromVariantList(cards)},
        {"variants",QJsonArray::fromVariantList(variants)},{"history",QJsonArray::fromVariantList(history)},
        {"deleted",QJsonArray::fromVariantList(deleted)},{"checkpoints",QJsonArray::fromVariantList(checkpoints)}};
    const auto validated = model::collectionFromJson(root);
    if (!validated) {fail("EXPORT_LIMIT","The collection could not be saved as a portable backup.",validated.error());return;}
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
    QSet<QString> checkpointMediaNames;
    for (const QVariantMap &record : records.checkpoints) {
        QSqlQuery existing(m_db);
        existing.prepare("SELECT 1 FROM deck_checkpoints WHERE id=?");
        existing.addBindValue(record.value("id"));
        if (!existing.exec()) {m_sqlDetail=existing.lastError().text();return false;}
        if (!existing.next() && !execute("INSERT INTO deck_checkpoints(id,deck_id,created_at,note,summary,snapshot,changes) VALUES(?,?,?,?,?,?,?)",
            {record.value("id"),record.value("deckId"),record.value("createdAt"),record.value("note"),record.value("summary"),
             json(record.value("snapshot").toMap()),json(record.value("changes").toMap())})) return false;
        for (const QVariant &value : record.value("snapshot").toMap().value("cards").toList()) {
            const auto note = model::cardFromMap(value.toMap());
            assert(note.has_value());
            const auto names = model::cardMediaReferences(*note);
            assert(names.has_value());
            for (const QString &name : *names) checkpointMediaNames.insert(name);
        }
    }
    for (const struct model::MediaAttachment &attachment : records.media) {
        if (!checkpointMediaNames.contains(attachment.name)) continue;
        QSqlQuery existing(m_db);
        existing.prepare("SELECT bytes FROM checkpoint_media WHERE name=?");
        existing.addBindValue(attachment.name);
        if (!existing.exec()) {m_sqlDetail=existing.lastError().text();return false;}
        if (existing.next()) {
            if (existing.value(0).toByteArray() != attachment.bytes) {m_sqlDetail="Preserved checkpoint media has conflicting content: " + attachment.name;return false;}
        } else if (!execute("INSERT INTO checkpoint_media(name,bytes) VALUES(?,?)",{attachment.name,attachment.bytes})) return false;
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
        QSqlQuery q(m_db);q.prepare("SELECT id,card_id,variant_id,deck_name,grade,recall_fraction,response_seconds,reviewed_at,due,session_id,session_deck_name,session_started_at FROM reviews WHERE id=?");q.addBindValue(record.id);
        if (!q.exec()) {fail("IMPORT_READ","Could not check existing review history.",q.lastError().text());return;}
        if (q.next()&&model::toMap(readReview(q))!=model::toMap(record)) {collision("Review "+record.id);return;}
    }
    for (const struct model::DeletedRecord &record : records.deleted) {
        if ((record.type==QStringLiteral("deck")&&deck(record.id))||(record.type==QStringLiteral("card")&&card(record.id))||(record.type==QStringLiteral("variant")&&variant(record.id))) {collision("Deletion "+record.id);return;}
    }
    for (const QVariantMap &record : records.checkpoints) {
        QSqlQuery q(m_db);
        q.prepare("SELECT deck_id,created_at,note,summary,snapshot,changes FROM deck_checkpoints WHERE id=?");
        q.addBindValue(record.value("id"));
        if (!q.exec()) {fail("IMPORT_READ","Could not check existing deck checkpoints.",q.lastError().text());return;}
        if (!q.next()) continue;
        const QJsonDocument snapshot = QJsonDocument::fromJson(q.value(4).toString().toUtf8());
        const QJsonDocument changes = QJsonDocument::fromJson(q.value(5).toString().toUtf8());
        if (!snapshot.isObject() || !changes.isObject()) {collision("Damaged checkpoint " + record.value("id").toString());return;}
        const auto existing = model::checkpointFromMap({{"id",record.value("id")},{"deckId",q.value(0)},{"createdAt",q.value(1)},
            {"note",q.value(2)},{"summary",q.value(3)},{"snapshot",snapshot.object().toVariantMap()},{"changes",changes.object().toVariantMap()}});
        if (!existing || *existing != record) {collision("Checkpoint " + record.value("id").toString());return;}
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
void DatabaseWorker::loadCardTimeline(const QString &cardId, int offset, const QString &requestId)
{
    assertThread();
    const auto makeError = [&](const QString &code, const QString &message, const QString &details = QString()) {
        return QVariantMap{{"requestId", requestId}, {"offset", offset}, {"events", QVariantList{}}, {"totalEvents", 0}, {"hasMore", false}, {"nextOffset", 0},
                           {"reviewCount", 0}, {"successfulCount", 0}, {"averageRecall", 0.0},
                           {"averageSeconds", 0.0}, {"averageGrade", QVariant()}, {"bestSuccessfulSeconds", QVariant()},
                           {"hasSuccessfulReview", false}, {"firstRecordedAt", QString()}, {"firstSavedAt", QString()},
                           {"error", QVariantMap{{"code", code}, {"message", message}, {"details", details}}}};
    };
    m_sqlDetail.clear();
    if (!m_db.isValid() || !m_db.isOpen()) {
        emit cardTimelineReady(cardId, makeError(QStringLiteral("STORAGE_UNAVAILABLE"), QStringLiteral("Could not load card history. Storage is unavailable.")));
        return;
    }
    if (!model::validUuid(cardId)) {
        emit cardTimelineReady(cardId, makeError(QStringLiteral("CARD_ID_INVALID"), QStringLiteral("Choose a saved card to view its history.")));
        return;
    }
    const auto timelineCard = card(cardId);
    if (!timelineCard) {
        emit cardTimelineReady(cardId, makeError(QStringLiteral("CARD_NOT_FOUND"), QStringLiteral("This card is unavailable. Refresh the collection."), m_sqlDetail));
        return;
    }
    QStringList timelineVariantKeys;
    const auto keys = model::variantKeys(*timelineCard);
    if (keys) timelineVariantKeys = *keys;
    if (offset < 0 || offset > 1000000) {
        emit cardTimelineReady(cardId, makeError(QStringLiteral("TIMELINE_OFFSET_INVALID"), QStringLiteral("The requested history page is invalid.")));
        return;
    }

    struct TimelineEvent {
        QString at;
        QVariantMap value;
    };
    QList<struct TimelineEvent> events;
    struct CardSnapshotEvent { qint64 rowId = 0; QString at; QString id; QVariantMap snapshot; };
    QList<struct CardSnapshotEvent> snapshots;
    QString firstSavedAt;
    QString firstRecordedAt;
    auto considerRecorded = [&](const QString &at) {
        if (at.isEmpty()) return;
        if (firstRecordedAt.isEmpty() || at < firstRecordedAt) firstRecordedAt = at;
    };

    QSqlQuery local(m_db);
    local.prepare("SELECT rowid,id,event FROM local_events WHERE target_type='card' AND target_id=? ORDER BY rowid");
    local.addBindValue(cardId);
    if (!local.exec()) {
        emit cardTimelineReady(cardId, makeError(QStringLiteral("TIMELINE_READ"), QStringLiteral("Could not load the card timeline. Refresh and try again."), local.lastError().text()));
        return;
    }
    QVariantMap previousCard;
    bool hasPreviousCard = false;
    while (local.next()) {
        const QJsonDocument document = QJsonDocument::fromJson(local.value(2).toString().toUtf8());
        if (!document.isObject()) {
            emit cardTimelineReady(cardId, makeError(QStringLiteral("TIMELINE_DATA"), QStringLiteral("Card history contains an invalid saved event."), QStringLiteral("local_events row %1 is not a JSON object.").arg(local.value(0).toLongLong())));
            return;
        }
        const QVariantMap envelope = document.object().toVariantMap();
        const QString eventType = envelope.value("type").toString();
        if (eventType == QStringLiteral("card.delete")) continue;
        if (eventType != QStringLiteral("card.upsert")) {
            emit cardTimelineReady(cardId, makeError(QStringLiteral("TIMELINE_DATA"), QStringLiteral("Card history contains an unsupported saved event."), eventType));
            return;
        }
        const QVariantMap payload = envelope.value("payload").toMap();
        const QVariantMap snapshot = payload.value("card").toMap();
        const auto validSnapshot = model::cardFromMap(snapshot);
        if (!validSnapshot || snapshot.value("id").toString() != cardId) {
            emit cardTimelineReady(cardId, makeError(QStringLiteral("TIMELINE_DATA"), QStringLiteral("Card history contains an invalid saved card snapshot."), validSnapshot ? QStringLiteral("The event targets a different card.") : validSnapshot.error()));
            return;
        }
        const QString at = envelope.value("createdAt").toString();
        if (!model::utcInstant(at)) {
            emit cardTimelineReady(cardId, makeError(QStringLiteral("TIMELINE_DATA"), QStringLiteral("Card history contains an invalid event timestamp."), at));
            return;
        }
        snapshots.append({local.value(0).toLongLong(), at, local.value(1).toString(), snapshot});
    }
    std::sort(snapshots.begin(), snapshots.end(), [](const struct CardSnapshotEvent &left, const struct CardSnapshotEvent &right) {
        if (left.at != right.at) return left.at < right.at;
        return left.rowId < right.rowId;
    });
    for (const struct CardSnapshotEvent &saved : snapshots) {
        const QVariantMap &snapshot = saved.snapshot;
        considerRecorded(saved.at);
        const bool contentChanged = !hasPreviousCard
            || snapshot.value("kind") != previousCard.value("kind")
            || snapshot.value("front") != previousCard.value("front")
            || snapshot.value("back") != previousCard.value("back")
            || snapshot.value("tags") != previousCard.value("tags")
            || snapshot.value("pointCount") != previousCard.value("pointCount");
        if (contentChanged) {
            const QString kind = hasPreviousCard ? QStringLiteral("modified") : QStringLiteral("saved");
            if (firstSavedAt.isEmpty()) firstSavedAt = saved.at;
            events.append({saved.at, {{"id", saved.id}, {"kind", kind}, {"at", saved.at}, {"variantId", QString()}}});
        }
        previousCard = snapshot;
        hasPreviousCard = true;
    }

    const int pageSize = 100;
    const int lifecycleCount = events.size();
    QSqlQuery reviewStats(m_db);
    reviewStats.prepare("SELECT COUNT(*),COALESCE(SUM(CASE WHEN grade>=2 THEN 1 ELSE 0 END),0),COALESCE(AVG(recall_fraction),0),COALESCE(AVG(response_seconds),0),COALESCE(MIN(reviewed_at),''),AVG(grade),MIN(CASE WHEN grade>=2 THEN response_seconds END) FROM reviews WHERE card_id=?");
    reviewStats.addBindValue(cardId);
    if (!reviewStats.exec() || !reviewStats.next()) {
        emit cardTimelineReady(cardId, makeError(QStringLiteral("TIMELINE_READ"), QStringLiteral("Could not load the card timeline. Refresh and try again."), reviewStats.lastError().text()));
        return;
    }
    const int reviewCount = reviewStats.value(0).toInt();
    const int successfulCount = reviewStats.value(1).toInt();
    const double averageRecall = reviewStats.value(2).toDouble();
    const double averageSeconds = reviewStats.value(3).toDouble();
    const QVariant averageGrade = reviewCount > 0 ? reviewStats.value(5) : QVariant{};
    const QVariant bestSuccessfulSeconds = successfulCount > 0 ? reviewStats.value(6) : QVariant{};
    assert(successfulCount == 0 || !bestSuccessfulSeconds.isNull());
    const QString earliestReviewAt = reviewStats.value(4).toString();
    if (!earliestReviewAt.isEmpty() && !model::utcInstant(earliestReviewAt)) {
        emit cardTimelineReady(cardId, makeError(QStringLiteral("TIMELINE_DATA"), QStringLiteral("Card history contains an invalid review timestamp."), earliestReviewAt));
        return;
    }
    considerRecorded(earliestReviewAt);
    QSqlQuery reviews(m_db);
    reviews.prepare("SELECT id,variant_id,grade,recall_fraction,response_seconds,reviewed_at FROM reviews WHERE card_id=? ORDER BY reviewed_at DESC,id DESC LIMIT ?");
    reviews.addBindValue(cardId);
    reviews.addBindValue(offset + pageSize);
    if (!reviews.exec()) {
        emit cardTimelineReady(cardId, makeError(QStringLiteral("TIMELINE_READ"), QStringLiteral("Could not load the card timeline. Refresh and try again."), reviews.lastError().text()));
        return;
    }
    while (reviews.next()) {
        const QString at = reviews.value(5).toString();
        if (!model::utcInstant(at)) {
            emit cardTimelineReady(cardId, makeError(QStringLiteral("TIMELINE_DATA"), QStringLiteral("Card history contains an invalid review timestamp."), at));
            return;
        }
        const int grade = reviews.value(2).toInt();
        const double recall = reviews.value(3).toDouble();
        const double seconds = reviews.value(4).toDouble();
        considerRecorded(at);
        const QString variantId = reviews.value(1).toString();
        QString variantKey;
        for (const QString &key : timelineVariantKeys) if (model::variantId(cardId, key) == variantId) { variantKey = key; break; }
        events.append({at, {{"id", reviews.value(0).toString()}, {"kind", QStringLiteral("review")}, {"at", at},
                            {"grade", grade}, {"recallFraction", recall}, {"responseSeconds", seconds},
                            {"variantId", variantId}, {"variantKey", variantKey}}});
    }

    std::sort(events.begin(), events.end(), [](const struct TimelineEvent &left, const struct TimelineEvent &right) {
        if (left.at != right.at) return left.at > right.at;
        return left.value.value("id").toString() > right.value.value("id").toString();
    });
    const int totalEvents = lifecycleCount + reviewCount;
    const int pageStart = std::min(offset, totalEvents);
    const int pageEnd = std::min(pageStart + pageSize, totalEvents);
    QVariantList page;
    if (pageStart < events.size()) {
        const int safePageEnd = std::min(pageEnd, static_cast<int>(events.size()));
        for (int index = safePageEnd - 1; index >= pageStart; --index) page.append(events.at(index).value);
    }
    const int nextOffset = std::min(offset + static_cast<int>(page.size()), totalEvents);
    const QVariantMap result{{"requestId", requestId}, {"offset", offset}, {"events", page}, {"totalEvents", totalEvents}, {"hasMore", nextOffset < totalEvents},
                             {"nextOffset", nextOffset}, {"reviewCount", reviewCount},
                             {"successfulCount", successfulCount}, {"averageRecall", averageRecall},
                             {"averageSeconds", averageSeconds}, {"averageGrade", averageGrade},
                             {"bestSuccessfulSeconds", bestSuccessfulSeconds}, {"hasSuccessfulReview", successfulCount > 0},
                             {"firstRecordedAt", firstRecordedAt}, {"firstSavedAt", firstSavedAt}, {"error", QVariantMap{}}};
    emit cardTimelineReady(cardId, result);
}

void DatabaseWorker::loadStudyAnalytics(const QStringList &deckIds, const QString &requestId)
{
    assertThread();
    QStringList scopeIds;
    QSet<QString> seenScopeIds;
    for (const QString &id : deckIds) {
        if (!seenScopeIds.contains(id)) {
            scopeIds.append(id);
            seenScopeIds.insert(id);
        }
    }
    const QDate today = QDateTime::currentDateTime().date();
    const QString todayString = today.toString(Qt::ISODate);
    const auto makeResult = [&](const QVariantList &days, const QVariantMap &stats, const QVariantMap &error,
                                const QString &firstDate = QString()) {
        return QVariantMap{{"requestId", requestId}, {"deckIds", QVariant::fromValue(scopeIds)}, {"today", todayString},
                           {"firstDate", firstDate.isEmpty() ? todayString : firstDate}, {"days", days},
                           {"stats", stats}, {"error", error}};
    };
    const auto makeError = [&](const QString &code, const QString &message, const QString &detail = QString()) {
        return makeResult({}, {}, {{"code", code}, {"message", message}, {"detail", detail}});
    };
    m_sqlDetail.clear();
    if (!m_db.isValid() || !m_db.isOpen()) {
        emit studyAnalyticsReady(scopeIds, makeError(QStringLiteral("STORAGE_UNAVAILABLE"), QStringLiteral("Could not load study analytics. Storage is unavailable."), m_sqlDetail));
        return;
    }
    for (const QString &deckId : scopeIds) {
        if (!model::validUuid(deckId)) {
            emit studyAnalyticsReady(scopeIds, makeError(QStringLiteral("DECK_ID_INVALID"), QStringLiteral("Choose saved decks to view their study analytics.")));
            return;
        }
    }
    if (!m_db.transaction()) {
        emit studyAnalyticsReady(scopeIds, makeError(QStringLiteral("ANALYTICS_READ"), QStringLiteral("Could not load a consistent study snapshot. Try refreshing."), m_db.lastError().text()));
        return;
    }
    auto rollbackSnapshot = qScopeGuard([this] { m_db.rollback(); });
    if (!scopeIds.isEmpty()) {
        QSqlQuery scope(m_db);
        scope.prepare("SELECT id FROM decks WHERE id=?");
        for (const QString &deckId : scopeIds) {
            scope.bindValue(0, deckId);
            if (!scope.exec()) {
                emit studyAnalyticsReady(scopeIds, makeError(QStringLiteral("ANALYTICS_READ"), QStringLiteral("Could not load study analytics. Refresh and try again."), scope.lastError().text()));
                return;
            }
            if (!scope.next()) {
                if (scope.lastError().isValid()) {
                    emit studyAnalyticsReady(scopeIds, makeError(QStringLiteral("ANALYTICS_READ"), QStringLiteral("Could not load study analytics. Refresh and try again."), scope.lastError().text()));
                    return;
                }
                emit studyAnalyticsReady(scopeIds, makeError(QStringLiteral("DECK_NOT_FOUND"), QStringLiteral("A selected deck is unavailable. Refresh the collection.")));
                return;
            }
            scope.finish();
        }
        QSqlQuery createRoots(m_db);
        if (!createRoots.exec(QStringLiteral("CREATE TEMP TABLE IF NOT EXISTS betterflash_analytics_roots(id TEXT PRIMARY KEY)"))) {
            emit studyAnalyticsReady(scopeIds, makeError(QStringLiteral("ANALYTICS_READ"), QStringLiteral("Could not prepare the study analytics scope."), createRoots.lastError().text()));
            return;
        }
        QSqlQuery createDecks(m_db);
        if (!createDecks.exec(QStringLiteral("CREATE TEMP TABLE IF NOT EXISTS betterflash_analytics_decks(id TEXT PRIMARY KEY)"))) {
            emit studyAnalyticsReady(scopeIds, makeError(QStringLiteral("ANALYTICS_READ"), QStringLiteral("Could not prepare the study analytics scope."), createDecks.lastError().text()));
            return;
        }
        QSqlQuery clearRoots(m_db);
        if (!clearRoots.exec(QStringLiteral("DELETE FROM betterflash_analytics_roots"))) {
            emit studyAnalyticsReady(scopeIds, makeError(QStringLiteral("ANALYTICS_READ"), QStringLiteral("Could not prepare the study analytics scope."), clearRoots.lastError().text()));
            return;
        }
        QSqlQuery clearDecks(m_db);
        if (!clearDecks.exec(QStringLiteral("DELETE FROM betterflash_analytics_decks"))) {
            emit studyAnalyticsReady(scopeIds, makeError(QStringLiteral("ANALYTICS_READ"), QStringLiteral("Could not prepare the study analytics scope."), clearDecks.lastError().text()));
            return;
        }
        QSqlQuery insertRoot(m_db);
        insertRoot.prepare(QStringLiteral("INSERT INTO betterflash_analytics_roots(id) VALUES(?)"));
        for (const QString &deckId : scopeIds) {
            insertRoot.bindValue(0, deckId);
            if (!insertRoot.exec()) {
                emit studyAnalyticsReady(scopeIds, makeError(QStringLiteral("ANALYTICS_READ"), QStringLiteral("Could not prepare the study analytics scope."), insertRoot.lastError().text()));
                return;
            }
        }
        QSqlQuery populateDecks(m_db);
        if (!populateDecks.exec(QStringLiteral("WITH RECURSIVE descendants(id) AS (SELECT id FROM betterflash_analytics_roots UNION SELECT d.id FROM decks d JOIN descendants ON d.parent_id=descendants.id) INSERT INTO betterflash_analytics_decks(id) SELECT id FROM descendants"))) {
            emit studyAnalyticsReady(scopeIds, makeError(QStringLiteral("ANALYTICS_READ"), QStringLiteral("Could not prepare the study analytics scope."), populateDecks.lastError().text()));
            return;
        }
    }

    QHash<QString, QString> variantDeck;
    QHash<QString, int> deckVariantCount;
    QSqlQuery current(m_db);
    const QString currentSql = scopeIds.isEmpty()
        ? QStringLiteral("SELECT n.deck_id,v.id FROM notes n JOIN review_variants v ON v.card_id=n.id ORDER BY n.deck_id,v.id")
        : QStringLiteral("SELECT n.deck_id,v.id FROM notes n JOIN review_variants v ON v.card_id=n.id JOIN betterflash_analytics_decks s ON s.id=n.deck_id ORDER BY n.deck_id,v.id");
    current.prepare(currentSql);
    if (!current.exec()) {
        emit studyAnalyticsReady(scopeIds, makeError(QStringLiteral("ANALYTICS_READ"), QStringLiteral("Could not load study analytics. Refresh and try again."), current.lastError().text()));
        return;
    }
    while (current.next()) {
        const QString sourceDeck = current.value(0).toString();
        const QString variantId = current.value(1).toString();
        if (sourceDeck.isEmpty() || variantId.isEmpty()) {
            emit studyAnalyticsReady(scopeIds, makeError(QStringLiteral("ANALYTICS_DATA"), QStringLiteral("Study analytics contains an invalid current review variant.")));
            return;
        }
        variantDeck.insert(variantId, sourceDeck);
        deckVariantCount[sourceDeck] = deckVariantCount.value(sourceDeck) + 1;
    }
    if (current.lastError().isValid()) {
        emit studyAnalyticsReady(scopeIds, makeError(QStringLiteral("ANALYTICS_READ"), QStringLiteral("Could not load study analytics. Refresh and try again."), current.lastError().text()));
        return;
    }

    struct ReviewPoint { qint64 at = 0; QString id; int grade = 0; double recall = 0.0; };
    QHash<QString, struct ReviewPoint> currentLatest;
    QHash<QString, struct ReviewPoint> previousLatest;
    struct Day { int count = 0; double recall = 0.0; };
    QMap<QString, struct Day> dayTotals;
    const QDate currentStart = today.addDays(-29);
    const QDate previousStart = today.addDays(-59);
    const QDate previousEnd = today.addDays(-30);
    int currentReviewCount = 0;
    QSqlQuery reviews(m_db);
    const QString reviewSql = scopeIds.isEmpty()
        ? QStringLiteral("SELECT id,variant_id,grade,recall_fraction,reviewed_at FROM reviews ORDER BY reviewed_at,id")
        : QStringLiteral("SELECT r.id,r.variant_id,r.grade,r.recall_fraction,r.reviewed_at FROM reviews r JOIN notes n ON n.id=r.card_id JOIN betterflash_analytics_decks s ON s.id=n.deck_id ORDER BY r.reviewed_at,r.id");
    reviews.prepare(reviewSql);
    if (!reviews.exec()) {
        emit studyAnalyticsReady(scopeIds, makeError(QStringLiteral("ANALYTICS_READ"), QStringLiteral("Could not load study analytics. Refresh and try again."), reviews.lastError().text()));
        return;
    }
    QString firstDate;
    while (reviews.next()) {
        const QString reviewId = reviews.value(0).toString();
        const QString variantId = reviews.value(1).toString();
        const int grade = reviews.value(2).toInt();
        const double recall = reviews.value(3).toDouble();
        const QString reviewedAt = reviews.value(4).toString();
        const QDateTime instant = QDateTime::fromString(reviewedAt, Qt::ISODateWithMs);
        if (reviewId.isEmpty() || variantId.isEmpty() || !instant.isValid() || grade < 0 || grade > 4 || !std::isfinite(recall) || recall < 0.0 || recall > 1.0) {
            emit studyAnalyticsReady(scopeIds, makeError(QStringLiteral("ANALYTICS_DATA"), QStringLiteral("Study analytics contains an invalid saved review."), reviewedAt));
            return;
        }
        const QDate localDate = instant.toLocalTime().date();
        if (!localDate.isValid() || localDate > today) continue;
        const QString localDateString = localDate.toString(Qt::ISODate);
        if (firstDate.isEmpty() || localDateString < firstDate) firstDate = localDateString;
        struct Day &day = dayTotals[localDateString];
        ++day.count; day.recall += recall;
        const bool inCurrent = localDate >= currentStart && localDate <= today;
        const bool inPrevious = localDate >= previousStart && localDate <= previousEnd;
        if (inCurrent) ++currentReviewCount;
        if (!variantDeck.contains(variantId)) continue;
        const struct ReviewPoint point{instant.toMSecsSinceEpoch(), reviewId, grade, recall};
        const auto later = [](const struct ReviewPoint &left, const struct ReviewPoint &right) {
            return left.at < right.at || (left.at == right.at && left.id < right.id);
        };
        if (inCurrent && (!currentLatest.contains(variantId) || later(currentLatest.value(variantId), point))) currentLatest.insert(variantId, point);
        if (inPrevious && (!previousLatest.contains(variantId) || later(previousLatest.value(variantId), point))) previousLatest.insert(variantId, point);
    }
    if (reviews.lastError().isValid()) {
        emit studyAnalyticsReady(scopeIds, makeError(QStringLiteral("ANALYTICS_READ"), QStringLiteral("Could not load study analytics. Refresh and try again."), reviews.lastError().text()));
        return;
    }

    QVariantList days;
    for (auto it = dayTotals.cbegin(); it != dayTotals.cend(); ++it)
        days.append(QVariantMap{{"date", it.key()}, {"count", it.value().count}, {"averageRecall", it.value().count > 0 ? it.value().recall / it.value().count : 0.0}});
    auto observedRecall = [](const QHash<QString, struct ReviewPoint> &latest) {
        double total = 0.0; for (const struct ReviewPoint &point : latest) total += point.recall;
        return latest.isEmpty() ? 0.0 : total / latest.size();
    };
    const double currentObservedRecall = observedRecall(currentLatest);
    const double previousObservedRecall = observedRecall(previousLatest);
    int activeDays = 0;
    for (auto it = dayTotals.cbegin(); it != dayTotals.cend(); ++it) {
        const QDate day = QDate::fromString(it.key(), Qt::ISODate);
        if (day >= currentStart && day <= today) ++activeDays;
    }
    QHash<QString, double> deckObservedRecall;
    QSet<QString> reviewedDeckSet;
    double gradeTotal = 0.0;
    for (auto variant = currentLatest.cbegin(); variant != currentLatest.cend(); ++variant) {
        const QString sourceDeck = variantDeck.value(variant.key());
        deckObservedRecall[sourceDeck] += variant.value().recall;
        reviewedDeckSet.insert(sourceDeck);
        gradeTotal += variant.value().grade;
    }
    const int eligibleDecks = deckVariantCount.size();
    const QVariant averageRecall = currentLatest.isEmpty() ? QVariant{} : QVariant(currentObservedRecall);
    const QVariant averageGrade = currentLatest.isEmpty() ? QVariant{} : QVariant(gradeTotal / currentLatest.size());
    double practiceTotal = 0.0;
    for (auto it = deckVariantCount.cbegin(); it != deckVariantCount.cend(); ++it) {
        practiceTotal += deckObservedRecall.value(it.key()) / it.value();
    }
    const QVariant practiceScore = eligibleDecks == 0 ? QVariant{} : QVariant(100.0 * practiceTotal / eligibleDecks);
    const QVariant recallChange = (currentLatest.isEmpty() || previousLatest.isEmpty()) ? QVariant{} : QVariant(100.0 * (currentObservedRecall - previousObservedRecall));
    assert(currentLatest.size() <= variantDeck.size());
    assert(reviewedDeckSet.size() <= eligibleDecks);
    assert(activeDays >= 0 && activeDays <= 30);
    const QVariantMap stats{{"practiceScore", practiceScore}, {"averageRecall", averageRecall}, {"averageGrade", averageGrade},
                            {"eligibleDecks", eligibleDecks}, {"reviewedDecks", reviewedDeckSet.size()}, {"totalVariants", variantDeck.size()},
                            {"reviewedVariants", currentLatest.size()}, {"activeDays", activeDays},
                            {"windowDays", 30}, {"recallChange", recallChange}, {"currentReviewCount", currentReviewCount}};
    current.finish();
    reviews.finish();
    if (!m_db.commit()) {
        emit studyAnalyticsReady(scopeIds, makeError(QStringLiteral("ANALYTICS_READ"), QStringLiteral("Could not finish reading study analytics. Try refreshing."), m_db.lastError().text()));
        return;
    }
    rollbackSnapshot.dismiss();
    emit studyAnalyticsReady(scopeIds, makeResult(days, stats, {}, firstDate));
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
