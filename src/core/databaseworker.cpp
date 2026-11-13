#include "databaseworker.h"
#include "scheduler.h"
#include "mediabackup.h"
#include "syncprotocol.h"
#include "studyguidance.h"
#include "cardreadability.h"
#include "checkfollowuptags.h"

#include <QCryptographicHash>
#include <QDate>
#include <QElapsedTimer>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMap>
#include <QRandomGenerator>
#include <QSaveFile>
#include <QScopeGuard>
#include <QSet>
#include <QSqlError>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QThread>
#include <QTimer>
#include <QTimeZone>
#include <algorithm>
#include <array>
#include <cassert>
#include <cerrno>
#include <cmath>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace model = betterflash::model;
namespace {
constexpr qint64 startupReviewCooldownMs = 24LL * 60 * 60 * 1000;
static_assert(startupReviewCooldownMs == 86400000);

std::optional<qint64> startupReviewTimestamp(const QVariant &value, const qint64 now)
{
    if (value.metaType().id()!=QMetaType::QString) return std::nullopt;
    const QString text=value.toString();
    bool valid=false;
    const qint64 timestamp=text.toLongLong(&valid);
    if (!valid || timestamp<0 || QString::number(timestamp)!=text || timestamp>now+startupReviewCooldownMs)
        return std::nullopt;
    return timestamp;
}

QVariantMap object(const QVariantMap &map, const QString &key) { return map.value(key).toMap(); }
QString json(const QVariantMap &map) { return QString::fromUtf8(QJsonDocument(QJsonObject::fromVariantMap(map)).toJson(QJsonDocument::Compact)); }
struct model::Deck readDeck(const QSqlQuery &q)
{
    struct model::Deck result{q.value(0).toString(),q.value(1).toString(),q.value(2).toString(),q.value(3).toString(),q.value(4).toString()};
    struct QJsonParseError error{};
    const QJsonDocument document = QJsonDocument::fromJson(q.value(5).toString().toUtf8(), &error);
    if (error.error != QJsonParseError::NoError || !document.isArray())
        result.tagVocabularyError = QStringLiteral("Deck %1 tag vocabulary is not a valid JSON array: %2.").arg(result.id, error.errorString());
    else {
        const auto vocabulary = model::tagVocabularyFromVariant(document.array().toVariantList());
        if (!vocabulary) result.tagVocabularyError = QStringLiteral("Deck %1 tag vocabulary is invalid: %2").arg(result.id, vocabulary.error());
        else if (QJsonArray::fromStringList(*vocabulary).toVariantList()!=document.array().toVariantList())
            result.tagVocabularyError = QStringLiteral("Deck %1 tag vocabulary contains non-normalized text. Preserve a recovery copy before repairing it.").arg(result.id);
        else result.tagVocabulary = *vocabulary;
    }
    return result;
}
QVariantMap readMetadata(const QString &source, const QString &field, QString *error)
{
    QJsonParseError parseError{};
    const QJsonDocument document=QJsonDocument::fromJson(source.toUtf8(), &parseError);
    if (parseError.error!=QJsonParseError::NoError || !document.isObject()) {
        if (error) {
            if (!error->isEmpty()) *error+=QStringLiteral(" ");
            *error+=QStringLiteral("Stored %1 metadata is not a JSON object (%2). A recovery copy is required before replacing it.")
                .arg(field, parseError.error==QJsonParseError::NoError?QStringLiteral("wrong JSON type"):parseError.errorString());
        }
        return {};
    }
    return document.object().toVariantMap();
}
struct model::Card readCard(const QSqlQuery &q, QString *metadataError=nullptr)
{
    if (metadataError) metadataError->clear();
    if (metadataError) {
        const int pointsType=q.value(6).metaType().id();
        const bool numericPoints=pointsType==QMetaType::LongLong||pointsType==QMetaType::Int||pointsType==QMetaType::Double;
        const double points=q.value(6).toDouble();
        if (!numericPoints||!std::isfinite(points)||std::floor(points)!=points||points<1||points>1000)
            *metadataError=QStringLiteral("Stored point_count value %1 must be an integer from 1 through 1000. Choose the correct point count when repairing the card.").arg(q.value(6).toString().left(128));
        for (const int column:{0,1,2,3,4,5,7,8})
            if (q.value(column).metaType().id()!=QMetaType::QString)
                *metadataError+=QStringLiteral(" Stored %1 must be text; preserve a recovery copy before repairing it.").arg(q.record().fieldName(column));
    }
    QString dateError;
    const auto date = [&q, &dateError](const QString &name) {
        const int index = q.record().indexOf(name);
        if (index < 0) return QString();
        const QVariant value = q.value(index);
        if (value.metaType().id() != QMetaType::QString) {
            dateError += QStringLiteral("Stored %1 must be text. ").arg(name); return QString();
        }
        if (value.toString().isEmpty()) return QString();
        const auto parsed = model::utcInstant(value.toString());
        if (!parsed) { dateError += QStringLiteral("Stored %1 is invalid: %2. ").arg(name, parsed.error()); return QString(); }
        return *parsed;
    };
    const QString createdAt=date(QStringLiteral("created_at"));
    QString modifiedAt=date(QStringLiteral("modified_at"));
    if (!createdAt.isEmpty() && !modifiedAt.isEmpty() && modifiedAt<createdAt) {
        dateError+=QStringLiteral("The stored last-modified time precedes the creation time. ");
        modifiedAt.clear();
    }
    return {q.value(0).toString(),q.value(1).toString(),q.value(2).toString(),q.value(3).toString(),q.value(4).toString(),q.value(5).toString(),q.value(6).toInt(),
            readMetadata(q.value(7).toString(),QStringLiteral("narrations"),metadataError),
            readMetadata(q.value(8).toString(),QStringLiteral("narration fingerprints"),metadataError),createdAt,modifiedAt,dateError};
}
struct model::Variant readVariant(const QSqlQuery &q, QString *storageError=nullptr)
{
    const int phaseColumn = q.record().indexOf(QStringLiteral("graduated"));
    assert(phaseColumn >= 0);
    const struct model::Variant result{q.value(0).toString(),q.value(1).toString(),q.value(2).toString(),q.value(3).toString(),q.value(4).toInt(),q.value(5).toDouble(),q.value(6).toDouble(),q.value(phaseColumn).toBool()};
    if (storageError) {
        QVariantMap raw=model::toMap(result);
        for (const int column:{0,1,2,3}) {
            if (q.value(column).metaType().id()!=QMetaType::QString) {
                *storageError=QStringLiteral("Stored schedule %1 field %2 must be text.").arg(result.id.left(128),q.record().fieldName(column));return result;
            }
        }
        raw["reviewCount"]=q.value(4);raw["stability"]=q.value(5);raw["difficulty"]=q.value(6);
        const QVariant phase=q.value(phaseColumn);
        const bool numericPhase=!phase.isNull()&&phase.metaType().id()!=QMetaType::QString&&phase.metaType().id()!=QMetaType::QByteArray;
        if (!numericPhase||(phase.toDouble()!=0.0&&phase.toDouble()!=1.0)) raw["graduated"]=phase;
        const auto valid=model::variantFromMap(raw);
        *storageError=valid?QString{}:QStringLiteral("Stored schedule %1 (direction %2, review count %3, graduated %4): %5")
            .arg(result.id.left(128),result.key.left(128),q.value(4).toString().left(128),phase.toString().left(128),valid.error());
    }
    return result;
}
struct model::Review readReview(const QSqlQuery &q, QString *storageError=nullptr)
{
    const struct model::Review result{q.value(0).toString(),q.value(1).toString(),q.value(2).toString(),q.value(3).toString(),q.value(4).toInt(),q.value(5).toDouble(),q.value(6).toDouble(),q.value(7).toString(),q.value(8).toString(),q.value(9).toString(),q.value(10).toString(),q.value(11).toString()};
    if (storageError) {
        QVariantMap raw=model::toMap(result);
        for (const int column:{0,1,2,3,7,8,9,10,11}) {
            if (q.value(column).metaType().id()!=QMetaType::QString) {
                *storageError=QStringLiteral("Stored review %1 field %2 must be text.").arg(result.id.left(128),q.record().fieldName(column));return result;
            }
        }
        raw["grade"]=q.value(4);raw["recallFraction"]=q.value(5);raw["responseSeconds"]=q.value(6);
        if (!q.value(9).toString().isEmpty()||!q.value(10).toString().isEmpty()||!q.value(11).toString().isEmpty()) {
            raw["sessionId"]=q.value(9);raw["sessionDeckName"]=q.value(10);raw["sessionStartedAt"]=q.value(11);
        }
        const auto valid=model::reviewFromMap(raw);
        *storageError=valid?QString{}:QStringLiteral("Stored review %1 (grade %2): %3").arg(result.id.left(128),q.value(4).toString().left(128),valid.error());
    }
    return result;
}
QVariantList variantMaps(const QList<struct model::Variant> &variants)
{
    QVariantList result;
    for (const struct model::Variant &v : variants) result.append(model::toMap(v));
    return result;
}

struct WeaknessSummary {
    QVariantMap strongest{};
    QVariantMap byVariant{};
};
struct WeaknessSummary aggregateWeakness(const QList<struct model::Variant> &variants,
                                        const QHash<QString, QVariantMap> &byVariant)
{
    static const QVariantMap unassessed{{"score", 0.0}, {"tier", QStringLiteral("unassessed")},
        {"label", QStringLiteral("Not assessed")}, {"reviewCount", 0},
        {"sampleCount", 0}, {"provisional", true}, {"reasons", QStringList{}},
        {"recommendation", QStringLiteral("Build a little review history before interpreting weakness.")},
        {"variantId", QString()}, {"variantKey", QString()}, {"factors", QVariantList{}}, {"assessed", false}};
    struct WeaknessSummary result{unassessed,{}};
    for (const struct model::Variant &variant : variants) {
        const QVariantMap candidate = byVariant.value(variant.id);
        result.byVariant.insert(variant.id,candidate);
        if (candidate.value("score").toDouble() > result.strongest.value("score").toDouble()
            || (!result.strongest.value("assessed").toBool() && candidate.value("assessed").toBool()))
            result.strongest = candidate;
    }
    return result;
}

QByteArray startupReviewFingerprint(const QVariantMap &card)
{
    static const QStringList fields{QStringLiteral("id"),QStringLiteral("deckId"),QStringLiteral("kind"),
        QStringLiteral("front"),QStringLiteral("back"),QStringLiteral("pointCount"),QStringLiteral("variantId"),
        QStringLiteral("variantKey"),QStringLiteral("due"),QStringLiteral("reviewCount"),QStringLiteral("stability"),
        QStringLiteral("difficulty"),QStringLiteral("graduated"),QStringLiteral("question"),QStringLiteral("answer")};
    QJsonObject value;
    for (const QString &field:fields) value.insert(field,QJsonValue::fromVariant(card.value(field)));
    return QCryptographicHash::hash(QJsonDocument(value).toJson(QJsonDocument::Compact),QCryptographicHash::Sha256);
}
}

DatabaseWorker::DatabaseWorker(QString path) : m_path(std::move(path)) {}
DatabaseWorker::~DatabaseWorker() { assert(!m_db.isValid()); }
void DatabaseWorker::assertThread() const { assert(thread() == QThread::currentThread()); }
void DatabaseWorker::shutdown()
{
    assertThread();
    cancelStartupReviewCandidate();
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
    if (m_db.commit()) { if (m_historyCapturing) m_historyCommitted = true; return true; }
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
    m_historySetupError.clear();m_guidanceSetupError.clear();
    if (!initializeHistory()) m_historySetupError=m_sqlDetail;
    if (!execute("CREATE INDEX IF NOT EXISTS reviews_variant_recent ON reviews(variant_id,card_id,reviewed_at DESC,id DESC)")
        ||!initializeStudyGuidance()) m_guidanceSetupError=m_sqlDetail;
    m_sqlDetail.clear();
    QSqlQuery query(m_db);
    if (!query.exec("SELECT key,value FROM metadata")) { fail("STORAGE_METADATA","Could not load device metadata.",query.lastError().text());shutdown();return; }
    while (query.next()) {
        if (query.value(0).toString() == QStringLiteral("device_id")) m_deviceId = query.value(1).toString();
        else if (query.value(0).toString() == QStringLiteral("sync_cursor")) m_cursor = query.value(1).toLongLong();
        else if (query.value(0).toString() == QStringLiteral("sync_collection_id")) m_collectionId = query.value(1).toString();
    }
    if (!model::validUuid(m_deviceId)||(!m_collectionId.isEmpty()&&!model::validUuid(m_collectionId))
        ||m_cursor<0||m_cursor>9007199254740991LL) {
        fail("STORAGE_METADATA","Device metadata is invalid. Preserve the database and inspect the details.");shutdown();return;
    }
    if (publishSnapshot()) done("Ready");
}
bool DatabaseWorker::migrate()
{
    QSqlQuery versionQuery(m_db);
    if (!versionQuery.exec("PRAGMA user_version") || !versionQuery.next()) { m_sqlDetail = versionQuery.lastError().text();return false; }
    const int version = versionQuery.value(0).toInt();
    versionQuery.finish();
    assert(!versionQuery.isActive());
    if (version > 10) {
        m_sqlDetail = QStringLiteral("Collection %1 has schema %2; this build supports schema 10. Update BetterFlash before opening this collection.").arg(m_path).arg(version);
        return false;
    }
    const bool migrating=version<10;
    if (migrating&&(m_db.tables().contains(QStringLiteral("notes"))||m_db.tables().contains(QStringLiteral("cards")))
        &&!createRecoveryBackup()) return false;
    if (migrating&&(!execute("PRAGMA foreign_keys=OFF")||!execute("PRAGMA ignore_check_constraints=ON"))) return false;
    const bool migrated=mutate([&] {
        const auto variantsSchema = [](const QString &name, const bool ifNotExists) {
            assert(name == QStringLiteral("review_variants") || name == QStringLiteral("review_variants_v8"));
            return QStringLiteral("CREATE TABLE%1 %2(id TEXT PRIMARY KEY,card_id TEXT NOT NULL REFERENCES notes(id) ON DELETE CASCADE,variant_key TEXT NOT NULL,due TEXT NOT NULL,review_count INTEGER NOT NULL CHECK(review_count>=0),stability REAL NOT NULL CHECK(stability BETWEEN (1.0/144.0) AND 3650),difficulty REAL NOT NULL CHECK(difficulty BETWEEN 0 AND 1),graduated INTEGER NOT NULL DEFAULT 0 CHECK(graduated IN (0,1)),CHECK(graduated=0 OR review_count>0),CHECK(graduated=1 OR review_count=0 OR stability<1),UNIQUE(card_id,variant_key))")
                .arg(ifNotExists ? QStringLiteral(" IF NOT EXISTS") : QString(), name);
        };
        const QStringList schema{
            "CREATE TABLE IF NOT EXISTS decks(id TEXT PRIMARY KEY,name TEXT NOT NULL,description TEXT NOT NULL,created_at TEXT NOT NULL,parent_id TEXT NOT NULL DEFAULT '',tag_vocabulary TEXT NOT NULL DEFAULT '[]')",
            "CREATE TABLE IF NOT EXISTS notes(id TEXT PRIMARY KEY,deck_id TEXT NOT NULL REFERENCES decks(id) ON DELETE CASCADE,kind TEXT NOT NULL,front TEXT NOT NULL,back TEXT NOT NULL,tags TEXT NOT NULL,point_count INTEGER NOT NULL CHECK(point_count BETWEEN 1 AND 1000),narrations TEXT NOT NULL DEFAULT '{}',narration_fingerprints TEXT NOT NULL DEFAULT '{}')",
            variantsSchema(QStringLiteral("review_variants"), true),
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
            "CREATE TABLE IF NOT EXISTS remote_deletion_scopes(seq INTEGER PRIMARY KEY,scopes TEXT NOT NULL)",
            "CREATE TABLE IF NOT EXISTS entity_sequences(entity_type TEXT NOT NULL,entity_id TEXT NOT NULL,seq INTEGER NOT NULL,PRIMARY KEY(entity_type,entity_id))",
            "CREATE TABLE IF NOT EXISTS tombstones(entity_type TEXT NOT NULL,entity_id TEXT NOT NULL,seq INTEGER NOT NULL,PRIMARY KEY(entity_type,entity_id))",
            "CREATE TABLE IF NOT EXISTS deck_checkpoints(position INTEGER PRIMARY KEY AUTOINCREMENT,id TEXT UNIQUE NOT NULL,deck_id TEXT NOT NULL,created_at TEXT NOT NULL,note TEXT NOT NULL,summary TEXT NOT NULL,snapshot TEXT NOT NULL,changes TEXT NOT NULL)",
            "CREATE INDEX IF NOT EXISTS checkpoints_deck_position ON deck_checkpoints(deck_id,position)",
            "CREATE TABLE IF NOT EXISTS checkpoint_media(name TEXT PRIMARY KEY,bytes BLOB NOT NULL)"
        };
        for (const QString &statement : schema) if (!execute(statement)) return false;
        if (version < 8) {
            const QString nextVariants = variantsSchema(QStringLiteral("review_variants_v8"), false);
            if (!execute(nextVariants)
                || !execute("INSERT INTO review_variants_v8 SELECT id,card_id,variant_key,due,review_count,stability,difficulty,CASE WHEN review_count>0 THEN 1 ELSE 0 END FROM review_variants")
                || !execute("DROP TABLE review_variants")
                || !execute("ALTER TABLE review_variants_v8 RENAME TO review_variants")
                || !execute("CREATE INDEX variants_due ON review_variants(due)")) {
                m_sqlDetail = QStringLiteral("Could not migrate review schedules in %1 to schema 8: %2. The migration will be rolled back; preserve this database before retrying.").arg(m_path, m_sqlDetail);
                return false;
            }
        }
        if (version < 3) {
            QSqlQuery columns(m_db);
            if (!columns.exec("PRAGMA table_info(decks)")) {m_sqlDetail=columns.lastError().text();return false;}
            bool hasParent=false;while (columns.next()) hasParent|=columns.value(1).toString()==QStringLiteral("parent_id");
            if (!hasParent&&!execute("ALTER TABLE decks ADD COLUMN parent_id TEXT NOT NULL DEFAULT ''")) return false;
        }
        if (version < 9) {
            QSqlQuery columns(m_db);
            if (!columns.exec("PRAGMA table_info(decks)")) { m_sqlDetail=columns.lastError().text(); return false; }
            bool hasVocabulary=false;
            while (columns.next()) hasVocabulary |= columns.value(1).toString()==QStringLiteral("tag_vocabulary");
            if (!hasVocabulary && !execute("ALTER TABLE decks ADD COLUMN tag_vocabulary TEXT NOT NULL DEFAULT '[]'")) return false;
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
        if (version < 10) {
            QSqlQuery columns(m_db);
            if (!columns.exec("PRAGMA table_info(notes)")) { m_sqlDetail=columns.lastError().text(); return false; }
            QSet<QString> names; while (columns.next()) names.insert(columns.value(1).toString());
            if (!names.contains("created_at") && !execute("ALTER TABLE notes ADD COLUMN created_at TEXT NOT NULL DEFAULT ''")) return false;
            if (!names.contains("modified_at") && !execute("ALTER TABLE notes ADD COLUMN modified_at TEXT NOT NULL DEFAULT ''")) return false;
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
                if (!upsertCard(note)) return false;
                if (!valid) continue;
                const QDate oldDue = QDate::fromString(old.value(7).toString(),Qt::ISODate);
                const QString due = oldDue.isValid() ? oldDue.startOfDay(QTimeZone::systemTimeZone()).toUTC().toString(Qt::ISODateWithMs) : model::nowUtc();
                for (const QString &key : *model::variantKeys(note)) {
                    const struct model::Variant schedule{model::variantId(note.id,key),note.id,key,due,old.value(8).toInt(),std::clamp(old.value(9).toDouble(),betterflash::scheduler::minimumStabilityDays,betterflash::scheduler::maximumStabilityDays),std::clamp(old.value(10).toDouble(),0.0,1.0),old.value(8).toInt()>0};
                    if (!upsertVariant(schedule)) return false;
                }
                migrated.append(note);
            }
            QSqlQuery allDecks(m_db);
            if (!allDecks.exec("SELECT id,name,description,created_at,parent_id,tag_vocabulary FROM decks")) {m_sqlDetail=allDecks.lastError().text();return false;}
            while (allDecks.next()) if (!enqueue("deck.upsert",{{"deck",model::toMap(readDeck(allDecks))}})) return false;
            for (const struct model::Card &note : migrated) {
                if (!model::cardReadIssue(note,cardVariants(note.id)).isEmpty()) continue;
                if (!enqueue("card.upsert",cardPayload(note))) return false;
            }
            if (m_db.tables().contains(QStringLiteral("history"))) {
                QSqlQuery history(m_db);
                if (!history.exec("SELECT id,card_id,deck_name,grade,recall_fraction,response_seconds,reviewed_at,due FROM history")) {m_sqlDetail=history.lastError().text();return false;}
                while (history.next()) {
                    const QString cardId = history.value(1).toString();
                    const int grade = QStringList{"Missed","Partial","Hard","Good","Easy"}.indexOf(history.value(3).toString());
                    const QDate oldDate = QDate::fromString(history.value(7).toString(),Qt::ISODate);
                    const auto reviewedAt = model::utcInstant(history.value(6).toString());
                    if (!model::validUuid(cardId) || grade < 0 || !oldDate.isValid() || !reviewedAt) continue;
                    const struct model::Review review{history.value(0).toString(),cardId,model::variantId(cardId,"forward"),history.value(2).toString(),grade,history.value(4).toDouble(),history.value(5).toDouble(),*reviewedAt,oldDate.startOfDay(QTimeZone::systemTimeZone()).toUTC().toString(Qt::ISODateWithMs)};
                    if (!model::reviewFromMap(model::toMap(review))) continue;
                    if (!insertReview(review)) return false;
                }
            }
        }
        QSqlQuery pending(m_db);
        if (!pending.exec("SELECT id,device_id,event_type,payload,created_at,target_type,target_id FROM sync_outbox")) {m_sqlDetail=pending.lastError().text();return false;}
        while (pending.next()) {
            const QJsonDocument payload=QJsonDocument::fromJson(pending.value(3).toString().toUtf8());
            if (!payload.isObject()) continue;
            const QVariantMap event{{"id",pending.value(0)},{"deviceId",pending.value(1)},{"type",pending.value(2)},{"payload",payload.object().toVariantMap()},{"createdAt",pending.value(4)}};
            if (!execute("INSERT OR IGNORE INTO local_events(id,target_type,target_id,event) VALUES(?,?,?,?)",{pending.value(0),pending.value(5),pending.value(6),json(event)})) return false;
        }
        return execute("PRAGMA user_version=10");
    });
    const QString migrationError=m_sqlDetail;
    if (migrating&&(!execute("PRAGMA ignore_check_constraints=OFF")||!execute("PRAGMA foreign_keys=ON"))) return false;
    if (!migrated) m_sqlDetail=migrationError;
    return migrated;
}
std::optional<struct model::Deck> DatabaseWorker::deck(const QString &id)
{
    QSqlQuery q(m_db);q.prepare("SELECT id,name,description,created_at,parent_id,tag_vocabulary FROM decks WHERE id=?");q.addBindValue(id);
    if (!q.exec()) {m_sqlDetail=q.lastError().text();return std::nullopt;}
    return q.next() ? std::optional<struct model::Deck>(readDeck(q)) : std::nullopt;
}
std::optional<struct model::Card> DatabaseWorker::card(const QString &id)
{
    QSqlQuery q(m_db);q.prepare("SELECT id,deck_id,kind,front,back,tags,point_count,narrations,narration_fingerprints,created_at,modified_at FROM notes WHERE id=?");q.addBindValue(id);
    if (!q.exec()) {m_sqlDetail=q.lastError().text();return std::nullopt;}
    if (!q.next()) { m_cardDateErrors.remove(id); return std::nullopt; }
    QString metadataError;
    const struct model::Card result=readCard(q,&metadataError);
    if (!result.dateMetadataError.isEmpty()) m_cardDateErrors.insert(id);
    else m_cardDateErrors.remove(id);
    const QVariantMap issue=model::cardReadIssue(result,cardVariants(id),metadataError);
    if (!issue.isEmpty()) m_cardLoadErrors.insert(id,issue);
    return result;
}
std::optional<struct model::Variant> DatabaseWorker::variant(const QString &id, const bool validateStored)
{
    QSqlQuery q(m_db);q.prepare("SELECT id,card_id,variant_key,due,review_count,stability,difficulty,graduated FROM review_variants WHERE id=?");q.addBindValue(id);
    if (!q.exec()) {m_sqlDetail=q.lastError().text();return std::nullopt;}
    if (!q.next()) return std::nullopt;
    QString storageError;
    const struct model::Variant result=readVariant(q,validateStored?&storageError:nullptr);
    if (!storageError.isEmpty()) {m_sqlDetail=storageError;return std::nullopt;}
    return result;
}
QList<struct model::Variant> DatabaseWorker::cardVariants(const QString &id)
{
    QList<struct model::Variant> result;
    QSqlQuery q(m_db);q.prepare("SELECT id,card_id,variant_key,due,review_count,stability,difficulty,graduated FROM review_variants WHERE card_id=? ORDER BY variant_key");q.addBindValue(id);
    if (!q.exec()) {m_sqlDetail=q.lastError().text();return result;}
    while (q.next()) {
        QString storageError;
        const struct model::Variant schedule=readVariant(q,&storageError);result.append(schedule);
        if (!storageError.isEmpty()) {
            m_invalidScheduleValues.insert(id);
            m_cardLoadErrors.insert(id,QVariantMap{{"code","CARD_SCHEDULE"},{"cardId",id},{"message","The saved schedule needs repair. Other cards remain available."},{"detail",storageError}});
        }
    }
    return result;
}
bool DatabaseWorker::upsertDeck(const struct model::Deck &r)
{
    if (!r.tagVocabularyError.isEmpty()) { m_sqlDetail = r.tagVocabularyError; return false; }
    const auto existing=deck(r.id);
    if (!m_sqlDetail.isEmpty()) return false;
    if (existing && !existing->tagVocabularyError.isEmpty()) { m_sqlDetail=existing->tagVocabularyError; return false; }
    const QString vocabulary = QString::fromUtf8(QJsonDocument(QJsonArray::fromStringList(r.tagVocabulary)).toJson(QJsonDocument::Compact));
    return execute("INSERT INTO decks(id,name,description,created_at,parent_id,tag_vocabulary) VALUES(?,?,?,?,?,?) ON CONFLICT(id) DO UPDATE SET name=excluded.name,description=excluded.description,created_at=excluded.created_at,parent_id=excluded.parent_id,tag_vocabulary=excluded.tag_vocabulary",{r.id,r.name,r.description,r.createdAt,r.parentId,vocabulary});
}
bool DatabaseWorker::upsertCard(const struct model::Card &r)
{
    if (!r.dateMetadataError.isEmpty()) { m_sqlDetail = QStringLiteral("Card %1: %2 Preserve a recovery copy before repairing its dates.").arg(r.id,r.dateMetadataError); return false; }
    QSqlQuery existing(m_db);
    if (!existing.prepare("SELECT created_at,modified_at FROM notes WHERE id=?")) { m_sqlDetail=existing.lastError().text(); return false; }
    existing.addBindValue(r.id);
    if (!existing.exec()) { m_sqlDetail=existing.lastError().text(); return false; }
    if (existing.next()) {
        QStringList dates;
        for (const int index : {0,1}) {
            const QVariant value=existing.value(index);
            if (value.metaType().id()!=QMetaType::QString || (!value.toString().isEmpty() && !model::utcInstant(value.toString()))) {
                m_sqlDetail=QStringLiteral("Card %1 has invalid saved %2 metadata. Preserve a recovery copy before repair.").arg(r.id,index==0?QStringLiteral("creation date"):QStringLiteral("last-modified date")); return false;
            }
            dates.append(value.toString().isEmpty()?QString():*model::utcInstant(value.toString()));
        }
        if (!dates.at(0).isEmpty() && !dates.at(1).isEmpty() && dates.at(1)<dates.at(0)) {
            m_sqlDetail=QStringLiteral("Card %1 has a saved last-modified date before its creation date. Preserve a recovery copy before repair.").arg(r.id); return false;
        }
    }
    if (existing.lastError().isValid()) { m_sqlDetail=existing.lastError().text(); return false; }
    existing.finish();
    return execute("INSERT INTO notes(id,deck_id,kind,front,back,tags,point_count,narrations,narration_fingerprints,created_at,modified_at) VALUES(?,?,?,?,?,?,?,?,?,?,?) ON CONFLICT(id) DO UPDATE SET deck_id=excluded.deck_id,kind=excluded.kind,front=excluded.front,back=excluded.back,tags=excluded.tags,point_count=excluded.point_count,narrations=excluded.narrations,narration_fingerprints=excluded.narration_fingerprints,created_at=excluded.created_at,modified_at=excluded.modified_at",{r.id,r.deckId,r.kind,r.front,r.back,r.tags,r.pointCount,json(r.narrations),json(r.narrationFingerprints),r.createdAt,r.modifiedAt});
}
bool DatabaseWorker::upsertVariant(const struct model::Variant &r,const QString &reviewId)
{
    const auto previous=variant(r.id);
    if (!m_sqlDetail.isEmpty()) return false;
    const bool unchanged=previous&&model::toMap(*previous)==model::toMap(r);
    if (!execute("INSERT INTO review_variants(id,card_id,variant_key,due,review_count,stability,difficulty,graduated) VALUES(?,?,?,?,?,?,?,?) ON CONFLICT(id) DO UPDATE SET due=excluded.due,review_count=excluded.review_count,stability=excluded.stability,difficulty=excluded.difficulty,graduated=excluded.graduated",{r.id,r.cardId,r.key,r.due,r.reviewCount,r.stability,r.difficulty,r.graduated})
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
{
    const auto persisted=card(record.id);
    if (!persisted) { if (m_sqlDetail.isEmpty()) m_sqlDetail=QStringLiteral("Card %1 could not be read after saving.").arg(record.id); return {}; }
    return {{"card",model::toMap(*persisted)},{"variants",variantMaps(cardVariants(record.id))}};
}
bool DatabaseWorker::enqueue(const QString &type, const QVariantMap &suppliedPayload)
{
    QVariantMap payload=suppliedPayload;
    if (m_historyCapturing && !payload.value("restore").toBool()
        && (type==QStringLiteral("deck.upsert") || type==QStringLiteral("card.upsert"))) {
        const bool deckEvent=type==QStringLiteral("deck.upsert");
        const QString entity=deckEvent?QStringLiteral("deck"):QStringLiteral("card");
        const QVariantMap record=object(payload,entity);
        const QString entityId=record.value("id").toString();
        QSqlQuery created(m_db);
        created.prepare("SELECT 1 FROM temp.history_capture WHERE entity=? AND id=? AND before_row IS NULL AND after_row IS NOT NULL");
        created.addBindValue(entity);created.addBindValue(entityId);
        if (!created.exec()) {m_sqlDetail=created.lastError().text();return false;}
        if (created.next()) {
            QString parentId=record.value(deckEvent?QStringLiteral("parentId"):QStringLiteral("deckId")).toString();
            QList<struct model::Deck> ancestors;
            QSet<QString> visited;
            while (!parentId.isEmpty()) {
                if (visited.contains(parentId)) {m_sqlDetail=QStringLiteral("The creation dependency contains a deck cycle.");return false;}
                visited.insert(parentId);
                const auto parent=deck(parentId);
                if (!parent) {if (m_sqlDetail.isEmpty()) m_sqlDetail=QStringLiteral("The creation dependency refers to a missing parent deck.");return false;}
                ancestors.prepend(*parent);parentId=parent->parentId;
            }
            for (const struct model::Deck &ancestor:ancestors)
                if (!enqueue("deck.upsert",{{"deck",model::toMap(ancestor)},{"restore",true},{"restoreDependency",true}})) return false;
            payload.insert("restore",true);
            if (deckEvent) payload.insert("restoreDependency",true);
            else {
                QVariantList ids;
                for (const QVariant &value:payload.value("variants").toList()) ids.append(value.toMap().value("id"));
                payload.insert("freshVariantIds",ids);
            }
        }
    }
    if (type==QStringLiteral("card.upsert") && !payload.contains("freshVariantIds") && m_historyCapturing) {
        QVariantList fresh;
        QSqlQuery created(m_db);
        created.prepare("SELECT 1 FROM temp.history_capture WHERE entity='variant' AND id=? AND before_row IS NULL AND after_row IS NOT NULL");
        for (const QVariant &value:payload.value("variants").toList()) {
            const QString id=value.toMap().value("id").toString();
            created.bindValue(0,id);
            if (!created.exec()) {m_sqlDetail=created.lastError().text();return false;}
            if (created.next()) fresh.append(id);
        }
        payload.insert("freshVariantIds",fresh);
    }
    QString targetType,targetId;
    if (type==QStringLiteral("history.apply")) {targetType="history";targetId=object(payload.value("changes").toList().first().toMap(),"after").value("id").toString();if (targetId.isEmpty()) targetId=payload.value("changes").toList().first().toMap().value("id").toString();}
    else if (type.startsWith(QStringLiteral("deck."))) {targetType="deck";targetId=type.endsWith(QStringLiteral("delete"))?payload.value("id").toString():object(payload,"deck").value("id").toString();}
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

bool DatabaseWorker::initializeStudyGuidance()
{
    assertThread();
    // Trigger writes must avoid duplicates explicitly: an enclosing upsert can
    // override a trigger statement's conflict-resolution policy.
    const QStringList statements{
        QStringLiteral("CREATE TEMP TABLE IF NOT EXISTS betterflash_study_dirty_variants(id TEXT PRIMARY KEY)"),
        QStringLiteral("INSERT OR IGNORE INTO betterflash_study_dirty_variants(id) SELECT id FROM review_variants"),
        QStringLiteral("CREATE TEMP TRIGGER IF NOT EXISTS betterflash_study_reviews_insert AFTER INSERT ON main.reviews BEGIN INSERT INTO betterflash_study_dirty_variants(id) SELECT NEW.variant_id WHERE NOT EXISTS(SELECT 1 FROM betterflash_study_dirty_variants WHERE id=NEW.variant_id); END"),
        QStringLiteral("CREATE TEMP TRIGGER IF NOT EXISTS betterflash_study_reviews_update AFTER UPDATE ON main.reviews BEGIN INSERT INTO betterflash_study_dirty_variants(id) SELECT OLD.variant_id WHERE NOT EXISTS(SELECT 1 FROM betterflash_study_dirty_variants WHERE id=OLD.variant_id); INSERT INTO betterflash_study_dirty_variants(id) SELECT NEW.variant_id WHERE NOT EXISTS(SELECT 1 FROM betterflash_study_dirty_variants WHERE id=NEW.variant_id); END"),
        QStringLiteral("CREATE TEMP TRIGGER IF NOT EXISTS betterflash_study_reviews_delete AFTER DELETE ON main.reviews BEGIN INSERT INTO betterflash_study_dirty_variants(id) SELECT OLD.variant_id WHERE NOT EXISTS(SELECT 1 FROM betterflash_study_dirty_variants WHERE id=OLD.variant_id); END"),
        QStringLiteral("CREATE TEMP TRIGGER IF NOT EXISTS betterflash_study_variants_insert AFTER INSERT ON main.review_variants BEGIN INSERT INTO betterflash_study_dirty_variants(id) SELECT NEW.id WHERE NOT EXISTS(SELECT 1 FROM betterflash_study_dirty_variants WHERE id=NEW.id); END"),
        QStringLiteral("CREATE TEMP TRIGGER IF NOT EXISTS betterflash_study_variants_update AFTER UPDATE ON main.review_variants BEGIN INSERT INTO betterflash_study_dirty_variants(id) SELECT OLD.id WHERE NOT EXISTS(SELECT 1 FROM betterflash_study_dirty_variants WHERE id=OLD.id); INSERT INTO betterflash_study_dirty_variants(id) SELECT NEW.id WHERE NOT EXISTS(SELECT 1 FROM betterflash_study_dirty_variants WHERE id=NEW.id); END"),
        QStringLiteral("CREATE TEMP TRIGGER IF NOT EXISTS betterflash_study_variants_delete AFTER DELETE ON main.review_variants BEGIN INSERT INTO betterflash_study_dirty_variants(id) SELECT OLD.id WHERE NOT EXISTS(SELECT 1 FROM betterflash_study_dirty_variants WHERE id=OLD.id); END"),
        QStringLiteral("CREATE TEMP TRIGGER IF NOT EXISTS betterflash_study_notes_update AFTER UPDATE OF front,back,kind,point_count ON main.notes BEGIN INSERT INTO betterflash_study_dirty_variants(id) SELECT v.id FROM main.review_variants v WHERE v.card_id=NEW.id AND NOT EXISTS(SELECT 1 FROM betterflash_study_dirty_variants d WHERE d.id=v.id); END")
    };
    for (const QString &statement : statements) {
        if (!execute(statement)) return false;
    }
    return true;
}

bool DatabaseWorker::publishSnapshot()
{
    assertThread();
    m_collectionIssues=m_recoveryNotices;
    m_cardLoadErrors.clear();
    m_cardDateErrors.clear();
    m_invalidScheduleValues.clear();
    m_hasUnreadableHistory=false;
    m_hasOrphanSchedules=false;
    QHash<QString,QString> rawScheduleErrors;
    QVariantList decks,cards,history;
    QHash<QString,int> deckIndexes;
    QHash<QString,QString> deckParents;
    QHash<QString,QList<struct model::Variant>> schedulesByCard;
    QHash<QString,struct model::Card> notes;
    const QString now=model::nowUtc();
    const QStringList countFields{QStringLiteral("cardCount"),QStringLiteral("totalCount"),QStringLiteral("variantCount"),
        QStringLiteral("dueCount"),QStringLiteral("laterCount"),QStringLiteral("newDueCount"),QStringLiteral("reviewDueCount"),QStringLiteral("invalidCardCount")};
    const auto warning=[this](const QString &code,const QString &message,const QString &detail) {
        m_collectionIssues.append(QVariantMap{{"code",code},{"message",message},{"detail",detail.left(4096)}});
    };
    QSqlQuery query(m_db);
    if (!query.exec("SELECT id,name,description,created_at,parent_id,tag_vocabulary FROM decks ORDER BY name,id")) {
        fail("STORAGE_READ","Could not read saved decks. No cards have been removed by this failed load. Check storage and retry.",query.lastError().text());return false;
    }
    while (query.next()) {
        const struct model::Deck savedDeck=readDeck(query);
        QVariantMap map=model::toMap(savedDeck);
        if (!savedDeck.tagVocabularyError.isEmpty()) {
            map.insert("tagVocabularyError",savedDeck.tagVocabularyError);
            warning(QStringLiteral("DECK_TAG_VOCABULARY"),QStringLiteral("A deck's tag vocabulary needs repair. Its cards remain available."),savedDeck.tagVocabularyError);
        }
        const QString id=map.value("id").toString();deckIndexes.insert(id,decks.size());deckParents.insert(id,map.value("parentId").toString());
        for (const QString &field:countFields) {map.insert(field,0);map.insert(QStringLiteral("own")+field.left(1).toUpper()+field.mid(1),0);}
        decks.append(map);
    }
    if (query.lastError().isValid()) {fail("STORAGE_READ","Deck reading stopped before completion. Preserve the collection and retry.",query.lastError().text());return false;}
    if (!query.exec("SELECT id,card_id,variant_key,due,review_count,stability,difficulty,graduated FROM review_variants ORDER BY variant_key")) {
        fail("STORAGE_READ","Could not read review schedules. No saved cards or learning history have been removed. Check storage and retry.",query.lastError().text());return false;
    }
    while (query.next()) {
        QString storageError;
        const struct model::Variant schedule=readVariant(query,&storageError);
        schedulesByCard[schedule.cardId].append(schedule);
        if (!storageError.isEmpty()) {m_invalidScheduleValues.insert(schedule.cardId);rawScheduleErrors.insert(schedule.cardId,storageError);}
    }
    if (query.lastError().isValid()) {fail("STORAGE_READ","Schedule reading stopped before completion. Preserve the collection and retry.",query.lastError().text());return false;}
    if (!query.exec("SELECT id,deck_id,kind,front,back,tags,point_count,narrations,narration_fingerprints,created_at,modified_at FROM notes ORDER BY id")) {
        fail("STORAGE_READ","Could not read saved cards. No cards have been removed by this failed load. Check storage and retry.",query.lastError().text());return false;
    }
    while (query.next()) {
        QString metadataError;
        const struct model::Card note=readCard(query,&metadataError);
        const QList<struct model::Variant> schedules=schedulesByCard.value(note.id);
        QVariantMap issue=model::cardReadIssue(note,schedules,metadataError);
        if (issue.isEmpty()&&rawScheduleErrors.contains(note.id))
            issue={{"code","CARD_SCHEDULE"},{"cardId",note.id},{"message","The saved review schedule is invalid. The card and its learning history are retained; other cards remain available."},{"detail",rawScheduleErrors.value(note.id)}};
        if (issue.isEmpty()&&!deckIndexes.contains(note.deckId))
            issue={{"code","CARD_DECK"},{"cardId",note.id},{"message","This card's deck is missing. Its saved content and history are retained. Choose an existing deck when repairing it."},{"detail",QStringLiteral("Card %1 refers to missing deck %2.").arg(note.id,note.deckId)}};
        if (!issue.isEmpty()) m_cardLoadErrors.insert(note.id,issue);
        QVariantMap map=model::toMap(note);
        if (!note.dateMetadataError.isEmpty()) {
            m_cardDateErrors.insert(note.id);
            map.insert("dateMetadataError",note.dateMetadataError);
            warning("CARD_DATE_METADATA","A card has invalid date metadata. Its content and review remain available; preserve a recovery copy before repairing its dates.",QStringLiteral("Card %1: %2").arg(note.id,note.dateMetadataError));
        }
        map.insert("loadError",issue);map.insert("reviewable",issue.isEmpty());
        notes.insert(note.id,note);cards.append(map);
    }
    if (query.lastError().isValid()) {fail("STORAGE_READ","Card reading stopped before completion. Preserve the collection and retry.",query.lastError().text());return false;}
    for (auto it=schedulesByCard.cbegin();it!=schedulesByCard.cend();++it) {
        if (!notes.contains(it.key())) {
            m_hasOrphanSchedules=true;
            warning("ORPHAN_SCHEDULE","A saved schedule refers to a missing card. The schedule is retained; readable cards remain available.",QStringLiteral("Missing card %1. Preserve the database and inspect review_variants before repair.").arg(it.key().left(128)));
        }
    }
    if (!m_historySetupError.isEmpty()) warning("UNDO_UNAVAILABLE","Undo could not be prepared. Your saved cards remain available; check storage and restart before changing cards.",m_historySetupError);
    if (!m_guidanceSetupError.isEmpty()||!refreshStudyGuidance()) {
        warning("STUDY_GUIDANCE_UNAVAILABLE","Study guidance is unavailable. Your decks and cards remain accessible, and readable cards can still be reviewed.",m_guidanceSetupError.isEmpty()?m_sqlDetail:m_guidanceSetupError);
        m_weaknessByVariant.clear();m_sqlDetail.clear();
    }
    for (QVariant &entry:cards) {
        QVariantMap map=entry.toMap();const QString id=map.value("id").toString();
        const struct model::Card note=notes.value(id);
        const QList<struct model::Variant> schedules=schedulesByCard.value(id);
        const bool readable=!m_cardLoadErrors.contains(id);
        QVariantList scheduleMaps;QString earliest;qint64 reviewCount=0;int dueCount=0;int newDue=0;int reviewDue=0;
        for (const struct model::Variant &schedule:schedules) {
            QVariantMap saved=model::toMap(schedule);
            if (readable) saved.insert("weakness",m_weaknessByVariant.value(schedule.id));
            scheduleMaps.append(saved);
            if (earliest.isEmpty()||schedule.due<earliest) earliest=schedule.due;
            reviewCount+=std::max(0,schedule.reviewCount);
            if (readable&&schedule.due<=now) {++dueCount;if (schedule.reviewCount==0) ++newDue;else ++reviewDue;}
        }
        map.insert("variants",scheduleMaps);map.insert("due",earliest);map.insert("reviewCount",reviewCount);map.insert("dueCount",dueCount);
        const struct WeaknessSummary unassessed{};
        const struct WeaknessSummary weakness=readable?aggregateWeakness(schedules,m_weaknessByVariant):unassessed;
        map.insert("weakness",weakness.strongest);
        map.insert("variantWeaknesses",readable?QVariant(weakness.byVariant):QVariant{});entry=map;
        if (deckIndexes.contains(note.deckId)) {
            QVariantMap deckMap=decks.at(deckIndexes.value(note.deckId)).toMap();
            const QVariantMap increments{{"cardCount",1},{"totalCount",1},{"variantCount",schedules.size()},{"dueCount",dueCount},
                {"laterCount",readable?schedules.size()-dueCount:0},{"newDueCount",newDue},{"reviewDueCount",reviewDue},{"invalidCardCount",readable?0:1}};
            for (const QString &field:countFields) {
                const QString own=QStringLiteral("own")+field.left(1).toUpper()+field.mid(1);
                deckMap[field]=deckMap.value(field).toLongLong()+increments.value(field).toLongLong();
                deckMap[own]=deckMap.value(own).toLongLong()+increments.value(field).toLongLong();
            }
            decks[deckIndexes.value(note.deckId)]=deckMap;
        }
        if (!readable&&m_collectionIssues.size()<100) m_collectionIssues.append(m_cardLoadErrors.value(id));
    }
    QHash<QString,int> childCounts;QList<QString> ready;
    bool hierarchyValid=true;
    for (const QString &id:deckIndexes.keys()) childCounts.insert(id,0);
    for (const QString &id:deckIndexes.keys()) {
        const QString parent=deckParents.value(id);
        if (parent.isEmpty()) continue;
        if (childCounts.contains(parent)) childCounts[parent]++;else hierarchyValid=false;
    }
    for (const QString &id:deckIndexes.keys()) if (childCounts.value(id)==0) ready.append(id);
    int processed=0;
    while (hierarchyValid&&!ready.isEmpty()) {
        const QString current=ready.takeLast();++processed;
        const QString parent=deckParents.value(current);if (parent.isEmpty()) continue;
        const QVariantMap child=decks.at(deckIndexes.value(current)).toMap();QVariantMap aggregate=decks.at(deckIndexes.value(parent)).toMap();
        for (const QString &field:countFields) aggregate[field]=aggregate.value(field).toLongLong()+child.value(field).toLongLong();
        decks[deckIndexes.value(parent)]=aggregate;
        const int remaining=childCounts.value(parent)-1;childCounts[parent]=remaining;if (remaining==0) ready.append(parent);
    }
    if (!hierarchyValid||processed!=deckIndexes.size()) {
        warning("DECK_HIERARCHY","The deck hierarchy needs repair. All loaded decks are shown at the top level; their saved relationships are retained.","A deck has a missing parent or a parent cycle. Edit the affected deck's parent after preserving a backup.");
        for (QVariant &entry:decks) {
            QVariantMap map=entry.toMap();map.insert("storedParentId",map.value("parentId"));map.insert("parentId",QString{});
            for (const QString &field:countFields) map[field]=map.value(QStringLiteral("own")+field.left(1).toUpper()+field.mid(1));
            entry=map;
        }
    }
    if (!query.exec("SELECT id,card_id,variant_id,deck_name,grade,recall_fraction,response_seconds,reviewed_at,due,session_id,session_deck_name,session_started_at FROM reviews ORDER BY reviewed_at DESC,id DESC LIMIT 200")) {
        warning("REVIEW_HISTORY_UNAVAILABLE","Review history could not be loaded. Card content remains available; saved history has not been removed.",query.lastError().text());
    } else while (query.next()) {
        QString storageError;
        const struct model::Review review=readReview(query,&storageError);
        QVariantMap map=model::toMap(review);
        if (!storageError.isEmpty()) {
            const QVariantMap issue{{"code","REVIEW_RECORD"},{"message","This saved review entry needs repair. Its stored data is retained."},{"detail",QStringLiteral("Review %1: %2").arg(review.id,storageError)}};
            map.insert("loadError",issue);if (m_collectionIssues.size()<100) m_collectionIssues.append(issue);
        } else map.insert("gradeLabel",model::gradeLabel(review.grade));
        history.append(map);
    }
    if (!query.exec("SELECT id,payload FROM sync_outbox ORDER BY position")) {
        warning("SYNC_STORAGE","Pending sync changes could not be inspected. Local cards remain available; preserve the database before attempting sync.",query.lastError().text());
    } else while (query.next()) {
        QJsonParseError error{};
        const QJsonDocument payload=QJsonDocument::fromJson(query.value(1).toString().toUtf8(),&error);
        if (!payload.isObject()&&m_collectionIssues.size()<100)
            warning("SYNC_STORAGE","A pending sync change needs repair. Local cards remain available; the original change is retained and sync will stop before uploading it.",
                QStringLiteral("sync_outbox event %1: %2. Preserve the database and repair the pending event before syncing.").arg(query.value(0).toString().left(128),error.error==QJsonParseError::NoError?QStringLiteral("payload must be a JSON object"):error.errorString()));
    }
    const auto sessions=reviewSessions();
    if (m_hasUnreadableHistory) warning("REVIEW_RECORD","Some saved review entries need repair. Their stored data is retained; readable cards remain available.","Session summaries exclude unreadable entries, including entries outside the recent History window. Preserve a raw SQLite recovery copy before repairing history.");
    if (m_db.tables().contains(QStringLiteral("history"))) warning("LEGACY_HISTORY_RETAINED","The collection retains its original legacy history table. Entries that could not be converted remain there unchanged.","Preserve the database and inspect the history table if past entries are absent from the current History view.");
    if (!sessions) warning("REVIEW_SESSIONS_UNAVAILABLE","Review-session summaries are unavailable. Your decks, cards and stored reviews are retained.",sessions.error());
    emit collectionIssuesReady(m_collectionIssues);
    emit snapshotReady(decks,cards,history,sessions?*sessions:QVariantList{});
    m_sqlDetail.clear();
    return true;
}

bool DatabaseWorker::refreshStudyGuidance()
{
    assertThread();
    QStringList dirtyIds;
    QSqlQuery dirty(m_db);
    if (!dirty.exec("SELECT id FROM betterflash_study_dirty_variants ORDER BY id")) { m_sqlDetail=dirty.lastError().text(); return false; }
    while (dirty.next()) dirtyIds.append(dirty.value(0).toString());
    if (dirty.lastError().isValid()) { m_sqlDetail=dirty.lastError().text(); return false; }
    if (dirtyIds.isEmpty()) return true;
    QHash<QString, QString> keys;
    QHash<QString, double> budgets;
    QHash<QString, int> activeReviewCounts;
    QSqlQuery variants(m_db);
    if (!variants.exec("SELECT v.id,v.variant_key,v.review_count,n.id,n.deck_id,n.kind,n.front,n.back,n.tags,n.point_count,n.narrations,n.narration_fingerprints FROM betterflash_study_dirty_variants d CROSS JOIN review_variants v ON v.id=d.id JOIN notes n ON n.id=v.card_id")) {
        m_sqlDetail=variants.lastError().text();
        return false;
    }
    while (variants.next()) {
        const QString id=variants.value(0).toString();
        const QString key=variants.value(1).toString();
        if (id.isEmpty()||key.isEmpty()) continue;
        const int scheduleReviewCount=variants.value(2).toInt();
        if (scheduleReviewCount < 0) continue;
        const struct model::Card note{variants.value(3).toString(),variants.value(4).toString(),variants.value(5).toString(),
                                      variants.value(6).toString(),variants.value(7).toString(),variants.value(8).toString(),
                                      variants.value(9).toInt(),variants.value(10).toString().isEmpty()?QVariantMap{}:QJsonDocument::fromJson(variants.value(10).toString().toUtf8()).object().toVariantMap(),
                                      variants.value(11).toString().isEmpty()?QVariantMap{}:QJsonDocument::fromJson(variants.value(11).toString().toUtf8()).object().toVariantMap()};
        const auto valid=model::cardFromMap(model::toMap(note));
        const auto supported=model::variantKeys(note);
        if (!valid||!supported||!supported->contains(key)||m_cardLoadErrors.contains(note.id)) continue;
        keys.insert(id,key);
        activeReviewCounts.insert(id,scheduleReviewCount);
        budgets.insert(id,model::readingBudget(model::question(note,key)));
    }
    if (variants.lastError().isValid()) {
        m_sqlDetail=variants.lastError().text();
        return false;
    }
    QHash<QString,QVector<betterflash::study::ReviewEvidence>> evidence;
    QHash<QString,int> reviewCounts=activeReviewCounts;
    QSqlQuery reviews(m_db);
    if (!reviews.exec("SELECT r.variant_id,r.grade,r.recall_fraction,r.response_seconds "
                     "FROM betterflash_study_dirty_variants d CROSS JOIN review_variants v ON v.id=d.id CROSS JOIN reviews r ON r.id IN ("
                     "SELECT recent.id FROM reviews recent INDEXED BY reviews_variant_recent WHERE recent.variant_id=v.id AND recent.card_id=v.card_id "
                     "ORDER BY recent.reviewed_at DESC,recent.id DESC LIMIT 12) "
                     "WHERE v.review_count>0 ORDER BY v.id,r.reviewed_at DESC,r.id DESC")) {
        m_sqlDetail=reviews.lastError().text();
        return false;
    }
    while (reviews.next()) {
        const QString variantId=reviews.value(0).toString();
        const int grade=reviews.value(1).toInt();
        const double recall=reviews.value(2).toDouble();
        const double seconds=reviews.value(3).toDouble();
        if (grade<0||grade>4||!std::isfinite(recall)||recall<0.0||recall>1.0||!std::isfinite(seconds)||seconds<0.0) {
            if (m_collectionIssues.size()<100)
                m_collectionIssues.append(QVariantMap{{"code","REVIEW_EVIDENCE"},{"message","Some saved review evidence is invalid and is excluded from guidance. The stored review and card remain available."},
                    {"detail",QStringLiteral("Review direction %1 has an invalid grade, recall fraction or response time.").arg(variantId)}});
            continue;
        }
        if (!keys.contains(variantId)) continue;
        const int activeCount=activeReviewCounts.value(variantId);
        if (evidence.value(variantId).size() < std::min(activeCount,12)) {
            evidence[variantId].append({grade,recall,seconds});
        }
    }
    if (reviews.lastError().isValid()) {
        m_sqlDetail=reviews.lastError().text();
        return false;
    }
    QHash<QString,QVariantMap> next;
    for (auto it=keys.cbegin();it!=keys.cend();++it)
        next.insert(it.key(),betterflash::study::assess(it.key(),it.value(),budgets.value(it.key()),reviewCounts.value(it.key()),evidence.value(it.key())));
    if (!execute("DELETE FROM betterflash_study_dirty_variants")) return false;
    for (const QString &id : dirtyIds) m_weaknessByVariant.remove(id);
    for (auto it=next.cbegin();it!=next.cend();++it) m_weaknessByVariant.insert(it.key(),it.value());
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
        QString storageError;
        const struct model::Review review=readReview(q,&storageError);
        if (!storageError.isEmpty()) {m_hasUnreadableHistory=true;continue;}
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
            m_hasUnreadableHistory=true;
            continue;
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
    assertThread();
    if (!m_db.isOpen()) { initialize(); return; }
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
    result.insert("graduated",schedule.graduated);
    result.insert("question",model::question(note,schedule.key));result.insert("answer",model::answer(note,schedule.key));
    result.insert("readingBudgetSeconds",model::readingBudget(result.value("question").toString()));result.insert("deckName",deckName);
    const QVariantMap weakness = m_weaknessByVariant.value(schedule.id);
    if (!weakness.isEmpty()) result.insert("weakness", weakness);
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
    const auto schedule=variant(variantId,true);
    const auto note=schedule?card(schedule->cardId):std::nullopt;
    const auto sourceDeck=note?deck(note->deckId):std::nullopt;
    if (!schedule||!note||!sourceDeck) return std::nullopt;
    const auto keys=model::variantKeys(*note);
    if (!model::cardFromMap(model::toMap(*note))||!model::variantFromMap(model::toMap(*schedule))
        ||!keys||!keys->contains(schedule->key)||m_cardLoadErrors.contains(note->id)) return std::nullopt;
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
        if (m_startupSession) q.setForwardOnly(true);
        const QString sql=QStringLiteral("SELECT v.id,v.card_id,v.variant_key,v.due,v.review_count,v.stability,v.difficulty,n.deck_id,n.kind,n.front,n.back,n.tags,n.point_count,n.narrations,n.narration_fingerprints,d.name,v.graduated AS graduated FROM review_variants v JOIN notes n ON n.id=v.card_id JOIN decks d ON d.id=n.deck_id")
            + (m_startupSession?QStringLiteral(" WHERE v.id=?"):QString{});
        if (!q.prepare(sql)) {fail("REVIEW_LOAD","Could not prepare the review queue. Refresh and try again.",q.lastError().text());return false;}
        if (m_startupSession) {
            assert(m_sessionOrder.size()==1);
            q.addBindValue(m_sessionOrder.constFirst());
        }
        if (!q.exec()) {fail("REVIEW_LOAD","Could not load the review queue. Refresh and try again.",q.lastError().text());return false;}
        while (q.next()) {
            QString storageError;
            const struct model::Variant schedule=readVariant(q,&storageError);
            if (!storageError.isEmpty()) continue;
            if (!members.contains(schedule.id)||(!m_sessionGrades.contains(schedule.id)&&schedule.due>now)) continue;
            const struct model::Card note{schedule.cardId,q.value(7).toString(),q.value(8).toString(),q.value(9).toString(),q.value(10).toString(),q.value(11).toString(),q.value(12).toInt(),q.value(13).toString().isEmpty()?QVariantMap{}:QJsonDocument::fromJson(q.value(13).toString().toUtf8()).object().toVariantMap(),q.value(14).toString().isEmpty()?QVariantMap{}:QJsonDocument::fromJson(q.value(14).toString().toUtf8()).object().toVariantMap()};
            const auto keys=model::variantKeys(note);
            if (!model::cardFromMap(model::toMap(note))||!model::variantFromMap(model::toMap(schedule))
                ||!keys||!keys->contains(schedule.key)||m_cardLoadErrors.contains(note.id)) continue;
            cache.insert(schedule.id,reviewCard(note,schedule,q.value(15).toString()));
        }
        if (q.lastError().isValid()) {fail("REVIEW_LOAD","Could not finish reading the review queue. Refresh and try again.",q.lastError().text());return false;}
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
    const struct HistoryScope history(this, QStringLiteral("Create deck"));
    if (!history) return;
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
    const struct HistoryScope history(this, QStringLiteral("Edit deck"));
    if (!history) return;
    if (!ready()) return;
    const auto existing=deck(id);
    if (!existing) {fail("DECK_NOT_FOUND","This deck no longer exists. Refresh the collection.",m_sqlDetail);return;}
    const struct model::Deck record{id,name.trimmed(),description,existing->createdAt,parentId,existing->tagVocabulary,existing->tagVocabularyError};
    const auto valid=model::deckFromMap(model::toMap(record));
    if (!valid) {fail("INVALID_DECK","Enter a valid deck name and description.",valid.error());return;}
    if (!validDeckParent(record.id,record.parentId)) {fail("INVALID_DECK_PARENT","Choose an existing parent deck that is not this deck or one of its descendants.");return;}
    if (!mutate([&]{return upsertDeck(record)&&enqueue("deck.upsert",{{"deck",model::toMap(record)}});})) {fail("DECK_SAVE","Could not save the deck. Check storage and try again.",m_sqlDetail);return;}
    m_queueDirty=true;
    if (publishSnapshot()&&publishQueue(false)) done("Deck updated");
}
void DatabaseWorker::setDeckTagVocabulary(const QString &deckId, const QStringList &tags, const QString &requestId,
                                              const QStringList &expectedVocabulary)
{
    if (!ready()) { emit deckTagVocabularySaved(deckId,requestId,false); return; }
    const auto reject = [&](const QString &code, const QString &message, const QString &detail = QString()) {
        fail(code,message,detail); emit deckTagVocabularySaved(deckId,requestId,false);
    };
    const auto valid=model::tagVocabularyFromVariant(QVariant(tags));
    const auto expected=model::tagVocabularyFromVariant(QVariant(expectedVocabulary));
    if (!model::validUuid(deckId) || !valid || !expected) {
        reject("DECK_TAGS_INVALID","Could not save the deck tags. Correct the tag list and try again.",
               !valid ? valid.error() : !expected ? expected.error() : QStringLiteral("The deck identifier is invalid.")); return;
    }
    const struct HistoryScope history(this,QStringLiteral("Edit deck tags"));
    if (!history) { emit deckTagVocabularySaved(deckId,requestId,false); return; }
    QString failureCode, failureMessage, failureDetail;
    bool changed=false;
    const bool committed=mutate([&] {
        const auto current=deck(deckId);
        if (!m_sqlDetail.isEmpty()) return false;
        if (!current || isDeleted("deck",deckId)) {
            failureCode=QStringLiteral("DECK_TAGS_MISSING");
            failureMessage=QStringLiteral("This deck no longer exists. Refresh the library and try again."); return false;
        }
        if (!current->tagVocabularyError.isEmpty()) {
            failureCode=QStringLiteral("DECK_TAGS_DAMAGED");
            failureMessage=QStringLiteral("The saved deck tag list needs repair. Preserve a recovery copy before changing it.");
            failureDetail=current->tagVocabularyError; return false;
        }
        if (current->tagVocabulary!=*expected) {
            failureCode=QStringLiteral("DECK_TAGS_CHANGED");
            failureMessage=QStringLiteral("The deck tag list changed while you were editing it. Reload it and review your changes."); return false;
        }
        if (current->tagVocabulary==*valid) return true;
        struct model::Deck updated=*current;
        updated.tagVocabulary=*valid;
        if (!upsertDeck(updated) || !enqueue("deck.upsert",{{"deck",model::toMap(updated)}})) return false;
        changed=true; return true;
    });
    if (!committed) {
        if (failureCode.isEmpty()) reject("DECK_TAGS_SAVE","Could not save the deck tags. No changes were saved. Check storage and try again.",m_sqlDetail);
        else reject(failureCode,failureMessage,failureDetail);
        return;
    }
    if (changed) {
        m_queueDirty=true;
        if (publishSnapshot() && publishQueue(false)) done(QStringLiteral("Deck tags saved"));
    } else done(QStringLiteral("Deck tags are unchanged"));
    emit deckTagVocabularySaved(deckId,requestId,true);
}

void DatabaseWorker::applySuggestedTags(const QVariantList &proposals, const QString &requestId, const QString &deckId, const QStringList &expectedVocabulary)
{
    if (!ready()) { emit suggestedTagsApplied(requestId,false); return; }
    const auto reject = [&](const QString &code, const QString &message, const QString &detail = QString()) {
        fail(code,message,detail); emit suggestedTagsApplied(requestId,false);
    };
    if (proposals.isEmpty() || proposals.size()>5000) {
        reject("SUGGESTED_TAGS_LIMIT","Choose between 1 and 5,000 cards to tag."); return;
    }
    const auto expectedTags=model::tagVocabularyFromVariant(QVariant(expectedVocabulary));
    if (!model::validUuid(deckId) || !expectedTags || expectedTags->isEmpty()) {
        reject("SUGGESTED_TAGS_VOCABULARY","The deck tag list is missing or invalid. Refresh the deck and generate suggestions again.",
               !expectedTags ? expectedTags.error() : QStringLiteral("A valid deck and nonempty tag list are required.")); return;
    }
    const QSet<QString> allowedTags(expectedTags->cbegin(),expectedTags->cend());
    struct ProposedTags { struct model::Card snapshot; QStringList tags; };
    QList<struct ProposedTags> checked;
    QSet<QString> seen;
    for (const QVariant &item : proposals) {
        if (item.metaType().id()!=QMetaType::QVariantMap) {
            reject("SUGGESTED_TAGS_INVALID","A selected tag suggestion is invalid. Generate the suggestions again."); return;
        }
        const QVariantMap proposal=item.toMap();
        const auto snapshot=model::cardFromMap(proposal.value("snapshot").toMap());
        const auto tags=model::tagVocabularyFromVariant(proposal.value("tags"));
        if (!snapshot || !tags || tags->isEmpty() || seen.contains(snapshot->id)) {
            reject("SUGGESTED_TAGS_INVALID","A selected card or tag suggestion is invalid or repeated. Generate the suggestions again.",
                   !snapshot ? snapshot.error() : !tags ? tags.error() : QStringLiteral("Card %1 has no tags or is repeated.").arg(snapshot->id)); return;
        }
        for (const QString &tag : *tags) {
            if (!allowedTags.contains(tag)) {
                reject("SUGGESTED_TAGS_INVALID","A suggested tag is no longer in this deck's tag list. Generate suggestions again.",tag.left(80)); return;
            }
        }
        seen.insert(snapshot->id); checked.append({*snapshot,*tags});
    }
    const struct HistoryScope history(this,QStringLiteral("Apply suggested tags"));
    if (!history) { emit suggestedTagsApplied(requestId,false); return; }
    QString failureCode, failureMessage, failureDetail;
    qsizetype changed=0;
    const bool committed=mutate([&] {
        const auto scope=deck(deckId);
        if (!m_sqlDetail.isEmpty()) return false;
        const bool scopeDeleted=scope && isDeleted("deck",deckId);
        if (!m_sqlDetail.isEmpty()) return false;
        if (!scope || scopeDeleted || !scope->tagVocabularyError.isEmpty() || scope->tagVocabulary!=*expectedTags) {
            failureCode=QStringLiteral("SUGGESTED_TAGS_VOCABULARY_CHANGED");
            failureMessage=QStringLiteral("The deck or its tag list changed after suggestions were generated. No tags were applied. Refresh and generate suggestions again.");
            failureDetail=scope ? scope->tagVocabularyError : QStringLiteral("The deck no longer exists."); return false;
        }
        QSqlQuery descendants(m_db);
        descendants.prepare("WITH RECURSIVE scope(id) AS (SELECT ? UNION SELECT d.id FROM decks d JOIN scope s ON d.parent_id=s.id) SELECT id FROM scope");
        descendants.addBindValue(deckId);
        if (!descendants.exec()) { m_sqlDetail=descendants.lastError().text(); return false; }
        QSet<QString> scopedDecks;
        while (descendants.next()) scopedDecks.insert(descendants.value(0).toString());
        if (descendants.lastError().isValid()) { m_sqlDetail=descendants.lastError().text(); return false; }
        QList<struct model::Card> updates;
        for (const struct ProposedTags &proposal : checked) {
            const QString &id=proposal.snapshot.id;
            if (!scopedDecks.contains(proposal.snapshot.deckId)) {
                failureCode=QStringLiteral("SUGGESTED_TAGS_SCOPE_CHANGED");
                failureMessage=QStringLiteral("A selected card is no longer in this deck or its subdecks. No tags were applied. Generate suggestions again.");
                failureDetail=QStringLiteral("Card %1 is outside deck %2.").arg(id,deckId); return false;
            }
            const auto current=card(id);
            if (!m_sqlDetail.isEmpty()) return false;
            const bool deleted=current && isDeleted("card",id);
            if (!m_sqlDetail.isEmpty()) return false;
            if (!current || deleted) {
                failureCode=QStringLiteral("SUGGESTED_TAGS_CARD_MISSING");
                failureMessage=QStringLiteral("A selected card no longer exists. No tags were applied. Refresh and generate suggestions again.");
                failureDetail=QStringLiteral("Card %1 no longer exists.").arg(id); return false;
            }
            if (m_cardLoadErrors.contains(id)) {
                failureCode=QStringLiteral("SUGGESTED_TAGS_CARD_UNREADABLE");
                failureMessage=QStringLiteral("A selected card needs repair. No tags were applied. Repair it and try again.");
                failureDetail=QStringLiteral("Card %1: %2").arg(id,m_cardLoadErrors.value(id).value("detail").toString()); return false;
            }
            if (model::toMap(*current)!=model::toMap(proposal.snapshot)) {
                failureCode=QStringLiteral("SUGGESTED_TAGS_CARD_CHANGED");
                failureMessage=QStringLiteral("A selected card changed after suggestions were generated. No tags were applied. Generate suggestions again.");
                failureDetail=QStringLiteral("Card %1 no longer matches the reviewed snapshot.").arg(id); return false;
            }
            struct model::Card updated=*current;
            QSet<QString> existing;
            for (const QString &tag : current->tags.split(QLatin1Char(','),Qt::SkipEmptyParts)) existing.insert(tag.trimmed().toCaseFolded());
            for (const QString &tag : proposal.tags) {
                if (existing.contains(tag.toCaseFolded())) continue;
                updated.tags += updated.tags.trimmed().isEmpty() ? tag : QStringLiteral(", ")+tag;
                existing.insert(tag.toCaseFolded());
            }
            if (updated.tags.size()>4096) {
                failureCode=QStringLiteral("SUGGESTED_TAGS_CARD_LIMIT");
                failureMessage=QStringLiteral("A selected card has too many tags. No tags were applied. Remove some tags and try again.");
                failureDetail=QStringLiteral("Card %1 would exceed the 4,096 character tag limit.").arg(id); return false;
            }
            if (updated.tags!=current->tags) updates.append(updated);
        }
        for (struct model::Card record : updates) {
            const auto previous=card(record.id);
            if (!previous || !m_sqlDetail.isEmpty()) return false;
            model::setCardEditDates(record,&*previous,model::nowUtc());
            if (!upsertCard(record)) return false;
            const QVariantMap payload=cardPayload(record);
            if (!m_sqlDetail.isEmpty() || !enqueue("card.upsert",payload)) return false;
        }
        changed=updates.size(); return true;
    });
    if (!committed) {
        if (failureCode.isEmpty()) reject("SUGGESTED_TAGS_SAVE","Could not apply the suggested tags. No changes were saved. Check storage and try again.",m_sqlDetail);
        else reject(failureCode,failureMessage,failureDetail);
        return;
    }
    if (changed) {
        m_queueDirty=true;
        if (publishSnapshot() && publishQueue(false)) done(QStringLiteral("Tagged %1 card(s)").arg(changed));
    } else done(QStringLiteral("Selected cards already have these tags"));
    emit suggestedTagsApplied(requestId,true);
}
void DatabaseWorker::moveDeck(const QString &id, const QString &parentId)
{
    const struct HistoryScope history(this, QStringLiteral("Move deck"));
    if (!history) { emit moveDeckFinished(false,id,parentId); return; }
    if (!ready()) { emit moveDeckFinished(false, id, parentId); return; }
    const auto existing = deck(id);
    if (!existing) { fail("DECK_NOT_FOUND", "This deck no longer exists. Refresh the collection.", m_sqlDetail); emit moveDeckFinished(false, id, parentId); return; }
    if (!validDeckParent(id, parentId)) { fail("INVALID_DECK_PARENT", "Choose an existing destination that is not this deck or one of its descendants."); emit moveDeckFinished(false, id, parentId); return; }
    if (existing->parentId == parentId) { done("Deck is already in that location"); emit moveDeckFinished(true, id, parentId); return; }
    struct model::Deck record=*existing;
    record.parentId=parentId;
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
    for (const QString &cardId:cardIds) {
        const auto target=card(cardId);
        if (!target) continue;
        const QVariantMap issue=model::cardReadIssue(*target,cardVariants(cardId));
        if (!issue.isEmpty()) m_cardLoadErrors.insert(cardId,issue);
    }
    const bool recovering=std::any_of(cardIds.cbegin(),cardIds.cend(),[this](const QString &cardId){return m_cardLoadErrors.contains(cardId)||m_cardDateErrors.contains(cardId);});
    if (recovering&&!createRecoveryBackup()) {fail("DECK_RECOVERY_BACKUP","Could not preserve damaged card data before deleting this deck. Nothing was deleted.",m_sqlDetail);return;}
    const struct HistoryScope history(this,QStringLiteral("Delete deck"),!recovering);
    if (!history) return;
    if (!mutate([&] {
        if (!eraseDeck(id)) return false;
        for (const QString &cardId : cardIds) if (!enqueue("card.delete",{{"id",cardId}})) return false;
        for (const QString &deckId : deckIds) if (!enqueue("deck.delete",{{"id",deckId}})) return false;
        return true;
    })) {fail("DECK_DELETE","Could not delete the deck. Check storage and try again.",m_sqlDetail);return;}
    if (recovering) clearHistoryForRecovery(QStringLiteral("The deck was deleted by request. Unreadable card data and learning records remain in the recovery copy."),m_lastRecoveryBackup);
    m_queueDirty=true;
    if (publishSnapshot()&&publishQueue(false)) done("Deck deleted");
}
bool DatabaseWorker::createRecoveryBackup()
{
    assertThread();
    const QString directory=QDir(QFileInfo(m_path).absolutePath()).filePath(QStringLiteral("recovery"));
    if (!QDir().mkpath(directory)||!QFile::setPermissions(directory,QFileDevice::ReadOwner|QFileDevice::WriteOwner|QFileDevice::ExeOwner)) {
        m_sqlDetail=QStringLiteral("Cannot create a private recovery directory at %1. Check its permissions and available space.").arg(directory);return false;
    }
    const QString destination=QDir(directory).filePath(QStringLiteral("before-repair-%1-%2.sqlite")
        .arg(QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMddTHHmmsszzzZ")),model::uuid()));
    QSqlQuery backup(m_db);
    if (!backup.prepare(QStringLiteral("VACUUM main INTO ?"))) {m_sqlDetail=backup.lastError().text();return false;}
    backup.addBindValue(destination);
    if (!backup.exec()) {m_sqlDetail=QStringLiteral("SQLite recovery copy %1 failed: %2").arg(destination,backup.lastError().text());return false;}
    if (!QFile::setPermissions(destination,QFileDevice::ReadOwner|QFileDevice::WriteOwner)) {
        m_sqlDetail=QStringLiteral("Cannot secure recovery copy %1. No card repair was applied.").arg(destination);return false;
    }
    m_lastRecoveryBackup=destination;
    return true;
}
void DatabaseWorker::clearHistoryForRecovery(const QString &message, const QString &backupPath)
{
    assertThread();
    m_historyFrames.clear();m_historyOrder.clear();
    if (!execute("DELETE FROM temp.history_frames")) {m_historyReady=false;m_historySetupError=m_sqlDetail;}
    emit historyClearedForRecovery();
    if (m_recoveryNotices.size()>=20) m_recoveryNotices.removeFirst();
    m_recoveryNotices.append(QVariantMap{{"code",backupPath.isEmpty()?QStringLiteral("UNDO_ACTION_UNAVAILABLE"):QStringLiteral("RECOVERY_COPY")},{"message",message},
        {"detail",(backupPath.isEmpty()?QStringLiteral("No recovery copy was created for this unexpected undo failure. Preserve a backup before further repairs."):
            QStringLiteral("Original collection copy: %1.").arg(backupPath))+QStringLiteral(" Session Undo/Redo was cleared; subsequent saved actions are undoable normally.")}});
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
                struct model::Card candidate=draft;
                candidate.narrations={{it.key(),it.value()}};candidate.narrationFingerprints.clear();
                if (!model::cardFromMap(model::toMap(candidate))) continue;
                narrations.insert(it.key(),it.value());
                candidate.narrationFingerprints={{it.key(),draft.narrationFingerprints.value(it.key())}};
                if (model::cardFromMap(model::toMap(candidate))) fingerprints.insert(it.key(),draft.narrationFingerprints.value(it.key()));
            }
    }
    const QString savedTags=id.isEmpty()?tags:betterflash::checkfollowup::withoutTags(tags);
    struct model::Card record{recordId,deckId,kind,front,back,savedTags,points,narrations,fingerprints};
    model::setCardEditDates(record,previous?&*previous:nullptr,model::nowUtc());
    const auto valid=model::cardFromMap(model::toMap(record));
    if (!valid) {failRequest(kind==QStringLiteral("cloze")?"INVALID_CLOZE":"INVALID_CARD","Could not save the card. Correct the card fields.",valid.error());return;}
    if (!validateMedia(record)) {failRequest("CARD_MEDIA","Could not save the card images. Restore missing images, insert them again, or split cards containing more than 64 MiB of images.",m_sqlDetail);return;}
    if (!deck(deckId) || isDeleted("deck",deckId)) {failRequest("DECK_NOT_FOUND","Choose an existing deck for this card.",m_sqlDetail);return;}
    if (!id.isEmpty() && (!card(id) || isDeleted("card",id))) {failRequest("CARD_NOT_FOUND","This card no longer exists. Refresh the collection.",m_sqlDetail);return;}
    QList<struct model::Variant> schedules;
    const QList<struct model::Variant> existing=cardVariants(record.id);
    if (!m_sqlDetail.isEmpty()) {failRequest("CARD_SCHEDULE_READ","Could not read this card's saved schedules. No repair was applied.",m_sqlDetail);return;}
    const bool repairing=previous&&(m_cardLoadErrors.contains(record.id)||!model::cardReadIssue(*previous,existing).isEmpty());
    if (repairing) {
        if (m_invalidScheduleValues.contains(record.id)) {
            failRequest("CARD_SCHEDULE_REPAIR_REQUIRED","The stored schedule has invalid values. Restore a valid recovery copy or explicitly reset the schedule; a content repair will not silently change learning records.",m_cardLoadErrors.value(record.id).value("detail").toString());return;
        }
        const auto repairedKeys=model::variantKeys(record);
        Q_ASSERT(repairedKeys);
        for (const struct model::Variant &saved:existing) {
            const auto validSaved=model::variantFromMap(model::toMap(saved));
            if (!validSaved) {failRequest("CARD_SCHEDULE_REPAIR_REQUIRED","The saved schedule needs an explicit repair or reset; saving card text will not silently change it.",validSaved.error());return;}
            if (!repairedKeys->contains(saved.key)) {
                failRequest("CARD_REPAIR_DIRECTIONS","Retain the saved review directions while repairing this card.",
                    QStringLiteral("Card %1 has a saved %2 direction. Restore that direction in the card text, or preserve a recovery copy before explicitly resetting its schedules.").arg(record.id,saved.key));return;
            }
        }
        if (!createRecoveryBackup()) {failRequest("CARD_RECOVERY_BACKUP","Could not preserve a recovery copy. No repair was applied. Check storage and retry.",m_sqlDetail);return;}
    }
    const struct HistoryScope history(this,QStringLiteral("Save card"),!repairing);
    if (!history) {emit cardSaved(recordId,requestId,false);return;}
    for (const QString &key : *model::variantKeys(record)) {
        const QString variantId=model::variantId(record.id,key);
        const auto found=std::find_if(existing.begin(),existing.end(),[&](const struct model::Variant &v){return v.id==variantId;});
        schedules.append(found==existing.end()?model::Variant{variantId,record.id,key,model::nowUtc()}:*found);
    }
    if (!mutate([&]{return upsertCard(record)&&replaceVariants(record,schedules)&&enqueue("card.upsert",cardPayload(record));})) {failRequest("CARD_SAVE","Could not save the card. Check storage and try again.",m_sqlDetail);return;}
    if (repairing) {
        clearHistoryForRecovery(QStringLiteral("The card was repaired. Its original stored data is retained in a recovery copy."),m_lastRecoveryBackup);
    }
    m_queueDirty=true;
    if (publishSnapshot()&&publishQueue(false)) done(repairing?QStringLiteral("Card repaired. Recovery copy: %1").arg(m_lastRecoveryBackup):QStringLiteral("Card saved"));
    emit cardSaved(recordId,requestId,true);
}
void DatabaseWorker::setCardNarration(const QString &cardId,const QString &variantKey,const QString &side,const QString &mediaName,const QString &requestId,const QString &fingerprint)
{
    const struct HistoryScope history(this, QStringLiteral("Change card narration"));
    if (!history) { emit cardNarrationSaved(cardId,variantKey,side,requestId,false); return; }
    if (!ready()) { emit cardNarrationSaved(cardId,variantKey,side,requestId,false); return; }
    const auto failRequest = [&] (const QString &code, const QString &message, const QString &detail = QString()) {
        fail(code,message,detail); emit cardNarrationSaved(cardId,variantKey,side,requestId,false);
    };
    const auto existing = card(cardId);
    if (!existing || isDeleted("card",cardId)) { failRequest("CARD_NOT_FOUND","This card no longer exists. Refresh the collection."); return; }
    if (m_cardLoadErrors.contains(cardId)) { failRequest("CARD_REPAIR_REQUIRED","Repair this card before changing its narration. Its saved content and learning history remain intact.",m_cardLoadErrors.value(cardId).value("detail").toString()); return; }
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
    model::setCardEditDates(updated,&*existing,model::nowUtc());
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
    const struct HistoryScope history(this, QStringLiteral("Confirm card narration"));
    if (!history) { emit cardNarrationSaved(cardId,variantKey,side,requestId,false); return; }
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
        if (m_cardLoadErrors.contains(cardId)) {
            failureCode = QStringLiteral("CARD_REPAIR_REQUIRED");
            failureMessage = QStringLiteral("Repair this card before confirming its narration. Its saved content and learning history remain intact.");
            m_sqlDetail = m_cardLoadErrors.value(cardId).value("detail").toString();
            return false;
        }
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
        model::setCardEditDates(updated,&*existing,model::nowUtc());
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
    const struct HistoryScope history(this, QStringLiteral("Atomicize card"));
    if (!history) { emit atomicSplitApplied(expectedSource.value("id").toString(),false,{}); return; }
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
        if (m_cardLoadErrors.contains(sourceId)) {m_sqlDetail=m_cardLoadErrors.value(sourceId).value("detail").toString();return false;}
        if (stored && !stored->dateMetadataError.isEmpty()) {m_sqlDetail=QStringLiteral("Card %1: %2 Preserve a recovery copy before repairing its dates.").arg(sourceId,stored->dateMetadataError);return false;}
        if (!stored||model::toMap(*stored)!=model::toMap(*expected)) {stale=true;return false;}
        if (!eraseCard(sourceId)||!enqueue("card.delete",{{"id",sourceId}})) return false;
        for (struct model::Card replacement:*replacements) {
            model::setCardEditDates(replacement,nullptr,model::nowUtc());
            if (!upsertCard(replacement)) return false;
            const auto keys = model::variantKeys(replacement);
            Q_ASSERT(keys);
            for (const QString &key : *keys) {
                const struct model::Variant schedule{model::variantId(replacement.id,key),replacement.id,key,model::nowUtc()};
                if (!upsertVariant(schedule)) return false;
            }
            if (!enqueue("card.upsert",cardPayload(replacement))) return false;
        }
        return true;
    });
    if (!committed) {
        if (stale) reject("ATOMIC_SPLIT_STALE","The original card changed or was deleted. Reopen Atomicize for the current saved card before preparing replacements.");
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
    const auto target=card(id);
    if (target) {
        const QVariantMap issue=model::cardReadIssue(*target,cardVariants(id));
        if (!issue.isEmpty()) m_cardLoadErrors.insert(id,issue);
    }
    const bool recovering=m_cardLoadErrors.contains(id)||m_cardDateErrors.contains(id);
    if (recovering&&!createRecoveryBackup()) {fail("CARD_RECOVERY_BACKUP","Could not preserve the card's damaged data before deletion. No card was deleted.",m_sqlDetail);return;}
    const struct HistoryScope history(this,QStringLiteral("Delete card"),!recovering);
    if (!history) return;
    if (!mutate([&]{return eraseCard(id)&&enqueue("card.delete",{{"id",id}});})) {fail("CARD_DELETE","Could not delete the card. Check storage and try again.",m_sqlDetail);return;}
    if (recovering) clearHistoryForRecovery(QStringLiteral("The card with damaged saved data was deleted by request. Its original content and learning records remain in the recovery copy."),m_lastRecoveryBackup);
    m_queueDirty=true;
    if (publishSnapshot()&&publishQueue(false)) done("Card deleted");
}
void DatabaseWorker::dismissCheckFollowUp(const QString &cardId, const QString &requestId,
                                          const QVariantMap &expectedSavedCard)
{
    if (!ready()) { emit checkFollowUpDismissed(cardId,requestId,false); return; }
    const auto reject = [&](const QString &code, const QString &message, const QString &detail = QString()) {
        fail(code,message,detail);
        emit checkFollowUpDismissed(cardId,requestId,false);
    };
    if (!model::validUuid(cardId)) {
        reject(QStringLiteral("CHECK_FOLLOW_UP_ID"),
            QStringLiteral("Could not dismiss the follow-up because the card identifier is invalid. Refresh the collection and try again."),
            QStringLiteral("Card identifier: %1").arg(cardId.left(128)));
        return;
    }
    if (!expectedSavedCard.isEmpty()) {
        for (const QString &field : {QStringLiteral("id"),QStringLiteral("front"),QStringLiteral("back"),
                                     QStringLiteral("kind"),QStringLiteral("tags")}) {
            if (expectedSavedCard.value(field).metaType().id()!=QMetaType::QString) {
                reject(QStringLiteral("CHECK_FOLLOW_UP_SNAPSHOT"),
                    QStringLiteral("Could not reset the follow-up because the saved card snapshot is incomplete. Refresh the collection and try again."),
                    QStringLiteral("Card %1 snapshot is missing text field %2.").arg(cardId,field));
                return;
            }
        }
        if (expectedSavedCard.value(QStringLiteral("id")).toString()!=cardId) {
            reject(QStringLiteral("CHECK_FOLLOW_UP_SNAPSHOT"),
                QStringLiteral("Could not reset the follow-up because the saved card snapshot does not match the selected card. Refresh the collection and try again."),
                QStringLiteral("Selected card %1 does not match snapshot card %2.")
                    .arg(cardId,expectedSavedCard.value(QStringLiteral("id")).toString().left(128)));
            return;
        }
    }
    const struct HistoryScope history(this,QStringLiteral("Dismiss check follow-up"));
    if (!history) { emit checkFollowUpDismissed(cardId,requestId,false); return; }
    QString failureCode;
    QString failureMessage;
    QString failureDetail;
    bool changed=false;
    const bool committed=mutate([&] {
        const auto current=card(cardId);
        if (!m_sqlDetail.isEmpty()) return false;
        const bool deleted=current && isDeleted(QStringLiteral("card"),cardId);
        if (!m_sqlDetail.isEmpty()) return false;
        if (!current || deleted) {
            failureCode=QStringLiteral("CHECK_FOLLOW_UP_MISSING");
            failureMessage=QStringLiteral("Could not dismiss the follow-up because the card no longer exists. Refresh the collection.");
            failureDetail=QStringLiteral("Card %1 is unavailable.").arg(cardId);
            return false;
        }
        if (m_cardLoadErrors.contains(cardId)) {
            failureCode=QStringLiteral("CHECK_FOLLOW_UP_UNREADABLE");
            failureMessage=QStringLiteral("Could not dismiss the follow-up because this card needs repair. Repair the card and try again.");
            failureDetail=m_cardLoadErrors.value(cardId).value(QStringLiteral("detail")).toString();
            return false;
        }
        if (!expectedSavedCard.isEmpty()
            && (current->front!=expectedSavedCard.value(QStringLiteral("front")).toString()
                || current->back!=expectedSavedCard.value(QStringLiteral("back")).toString()
                || current->kind!=expectedSavedCard.value(QStringLiteral("kind")).toString()
                || current->tags!=expectedSavedCard.value(QStringLiteral("tags")).toString())) {
            failureCode=QStringLiteral("CHECK_FOLLOW_UP_CHANGED");
            failureMessage=QStringLiteral("Could not reset the follow-up because the card or its tags changed. Refresh the card and run the check again.");
            failureDetail=QStringLiteral("Card %1 no longer matches the saved card snapshot.").arg(cardId);
            return false;
        }
        const QString retained=betterflash::checkfollowup::withoutTags(current->tags);
        if (retained==current->tags) return true;
        struct model::Card updated=*current;
        updated.tags=retained;
        model::setCardEditDates(updated,&*current,model::nowUtc());
        if (!upsertCard(updated)) return false;
        const QVariantMap payload=cardPayload(updated);
        if (!m_sqlDetail.isEmpty() || !enqueue(QStringLiteral("card.upsert"),payload)) return false;
        changed=true;
        return true;
    });
    if (!committed) {
        if (failureCode.isEmpty())
            reject(QStringLiteral("CHECK_FOLLOW_UP_SAVE"),
                QStringLiteral("Could not dismiss the follow-up. No changes were saved. Check storage and try again."),m_sqlDetail);
        else
            reject(failureCode,failureMessage,failureDetail);
        return;
    }
    if (!changed) {
        done(QStringLiteral("Card has no check follow-up"));
        emit checkFollowUpDismissed(cardId,requestId,true);
        return;
    }
    m_queueDirty=true;
    if (publishSnapshot() && publishQueue(false)) done(QStringLiteral("Check follow-up dismissed"));
    emit checkFollowUpDismissed(cardId,requestId,true);
}
void DatabaseWorker::markCardsForCheckReview(const QVariantList &expectedCards, const QString &mode, const QString &requestId)
{
    if (!ready()) { emit checkReviewMarked(requestId,false); return; }
    const auto reject = [&](const QString &code, const QString &message, const QString &detail = QString()) {
        fail(code,message,detail);
        emit checkReviewMarked(requestId,false);
    };
    if (expectedCards.isEmpty() || expectedCards.size() > model::maxCollectionRecords
        || (mode != QStringLiteral("correctness") && mode != QStringLiteral("completeness"))) {
        reject("CHECK_REVIEW_REQUEST","Choose at least one card from a correctness or completeness check."); return;
    }
    QSet<QString> seen;
    QList<QVariantMap> snapshots;
    for (const QVariant &value : expectedCards) {
        if (value.metaType().id()!=QMetaType::QVariantMap) {
            reject("CHECK_REVIEW_REQUEST","A selected card has an invalid check result. Run the deck check again."); return;
        }
        const QVariantMap snapshot=value.toMap();
        const QString id=snapshot.value("id").toString();
        if (!model::validUuid(id) || seen.contains(id)) {
            reject("CHECK_REVIEW_REQUEST","A selected card has an invalid or duplicate identifier. Run the deck check again.",id.left(128)); return;
        }
        for (const QString &field : {QStringLiteral("front"),QStringLiteral("back"),QStringLiteral("kind")}) {
            if (snapshot.value(field).metaType().id()!=QMetaType::QString) {
                reject("CHECK_REVIEW_REQUEST","A selected card's checked content is missing. Run the deck check again.",QStringLiteral("Card %1 is missing %2.").arg(id,field)); return;
            }
        }
        seen.insert(id);
        snapshots.append(snapshot);
    }
    const struct HistoryScope history(this,QStringLiteral("Mark checked cards for review"));
    if (!history) { emit checkReviewMarked(requestId,false); return; }
    const QString tag=mode==QStringLiteral("correctness")?QStringLiteral("check-correctness"):QStringLiteral("check-completeness");
    QString failureCode;
    QString failureMessage;
    QString failureDetail;
    qsizetype changed=0;
    const bool committed=mutate([&] {
        QList<struct model::Card> updates;
        for (const QVariantMap &snapshot : snapshots) {
            const QString id=snapshot.value("id").toString();
            const auto current=card(id);
            if (!m_sqlDetail.isEmpty()) return false;
            const bool deleted=current && isDeleted("card",id);
            if (!m_sqlDetail.isEmpty()) return false;
            if (!current || deleted) {
                failureCode=QStringLiteral("CHECK_REVIEW_CARD_MISSING");
                failureMessage=QStringLiteral("A selected card no longer exists. No cards were marked. Run the deck check again.");
                failureDetail=QStringLiteral("Card %1 is no longer available.").arg(id);
                return false;
            }
            if (m_cardLoadErrors.contains(id)) {
                failureCode=QStringLiteral("CHECK_REVIEW_CARD_UNREADABLE");
                failureMessage=QStringLiteral("A selected card needs repair. No cards were marked. Repair it and run the check again.");
                failureDetail=QStringLiteral("Card %1: %2").arg(id,m_cardLoadErrors.value(id).value("detail").toString());
                return false;
            }
            if (current->front!=snapshot.value("front").toString() || current->back!=snapshot.value("back").toString()
                || current->kind!=snapshot.value("kind").toString()) {
                failureCode=QStringLiteral("CHECK_REVIEW_CARD_CHANGED");
                failureMessage=QStringLiteral("A selected card changed after the check. No cards were marked. Run the deck check again.");
                failureDetail=QStringLiteral("Card %1 has different checked content.").arg(id);
                return false;
            }
            bool alreadyMarked=false;
            for (const QString &existing : current->tags.split(QLatin1Char(',')))
                if (existing.trimmed()==tag) { alreadyMarked=true; break; }
            if (alreadyMarked) continue;
            struct model::Card updated=*current;
            updated.tags += updated.tags.trimmed().isEmpty()?tag:QStringLiteral(", ")+tag;
            if (updated.tags.size()>4096) {
                failureCode=QStringLiteral("CHECK_REVIEW_TAG_LIMIT");
                failureMessage=QStringLiteral("A selected card has too many tags. No cards were marked. Remove a tag and try again.");
                failureDetail=QStringLiteral("Card %1 would exceed the 4096 character tag limit.").arg(id);
                return false;
            }
            updates.append(updated);
        }
        for (struct model::Card record : updates) {
            const auto previous=card(record.id);
            if (!previous || !m_sqlDetail.isEmpty()) return false;
            model::setCardEditDates(record,&*previous,model::nowUtc());
            if (!upsertCard(record)) return false;
            const QVariantMap payload=cardPayload(record);
            if (!m_sqlDetail.isEmpty() || !enqueue("card.upsert",payload)) return false;
        }
        changed=updates.size();
        return true;
    });
    if (!committed) {
        if (failureCode.isEmpty()) reject("CHECK_REVIEW_SAVE","Could not mark the selected cards. No changes were saved. Check storage and try again.",m_sqlDetail);
        else reject(failureCode,failureMessage,failureDetail);
        return;
    }
    if (!changed) {
        done("Selected cards are already marked for review");
        emit checkReviewMarked(requestId,true);
        return;
    }
    m_queueDirty=true;
    if (publishSnapshot() && publishQueue(false)) done(QStringLiteral("Marked %1 card(s) for review").arg(changed));
    emit checkReviewMarked(requestId,true);
}
void DatabaseWorker::moveCards(const QVariantList &cardIds, const QString &destinationDeckId, const QString &requestId)
{
    const struct HistoryScope history(this, QStringLiteral("Move cards"));
    if (!history) { emit moveCardsFinished(false,destinationDeckId,requestId); return; }
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
        if (m_cardLoadErrors.contains(id)) { reject("CARD_REPAIR_REQUIRED", "Repair the unreadable selected card before moving it. No selected cards were moved.", m_cardLoadErrors.value(id).value("detail").toString()); return; }
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
            struct model::Card moved=existing;
            moved.deckId=destinationDeckId;
            model::setCardEditDates(moved,&existing,model::nowUtc());
            if (!upsertCard(moved)) return false;
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
    QSqlQuery query(m_db);
    query.prepare(deckScope
        ? "WITH RECURSIVE descendants(id) AS (SELECT id FROM decks WHERE id=? UNION SELECT d.id FROM decks d JOIN descendants ON d.parent_id=descendants.id) SELECT id,deck_id,kind,front,back,tags,point_count,narrations,narration_fingerprints,created_at,modified_at FROM notes WHERE deck_id IN (SELECT id FROM descendants)"
        : "SELECT id,deck_id,kind,front,back,tags,point_count,narrations,narration_fingerprints,created_at,modified_at FROM notes WHERE id=?");
    query.addBindValue(id);
    if (!query.exec()) {fail("REVIEW_RESET_FAILED","Could not read the selected cards. No schedules were reset.",query.lastError().text());return;}
    QList<struct model::Card> cards;
    bool recovering=false;
    while (query.next()) {
        QString storageError;
        const struct model::Card note=readCard(query,&storageError);
        const auto valid=model::cardFromMap(model::toMap(note));
        if (!storageError.isEmpty()||!valid) {fail("CARD_REPAIR_REQUIRED","Repair this card's content before resetting its schedules. No schedules were reset.",storageError.isEmpty()?valid.error():storageError);return;}
        const auto keys=model::variantKeys(note);
        Q_ASSERT(keys);
        const QList<struct model::Variant> saved=cardVariants(note.id);
        for (const struct model::Variant &schedule:saved) {
            if (!keys->contains(schedule.key)||schedule.id!=model::variantId(note.id,schedule.key)) {
                fail("CARD_SCHEDULE_REPAIR_REQUIRED","A stored review direction does not match the card. Preserve the database and repair its directions before resetting schedules.",QStringLiteral("Card %1, schedule %2, direction %3.").arg(note.id,schedule.id,schedule.key));return;
            }
        }
        recovering=recovering||m_invalidScheduleValues.contains(note.id)||!model::cardReadIssue(note,saved).isEmpty();
        cards.append(note);
    }
    if (query.lastError().isValid()) {fail("REVIEW_RESET_FAILED","Card reading stopped before completion. No schedules were reset.",query.lastError().text());return;}
    query.finish();
    if (recovering&&!createRecoveryBackup()) {fail("CARD_RECOVERY_BACKUP","Could not preserve the original schedules. No reset was applied. Check storage and retry.",m_sqlDetail);return;}
    const struct HistoryScope history(this,QStringLiteral("Reset review state"),!recovering);
    if (!history) return;
    int count=0;
    const bool committed=mutate([&] {
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
            for (const struct model::Variant &schedule : cardVariants(note.id))
                if (!enqueue("variant.upsert",{{"variant",model::toMap(schedule)},{"reset",true}})) return false;
        }
        count=cards.size();
        return m_sqlDetail.isEmpty();
    });
    if (!committed) {fail("REVIEW_RESET_FAILED","Could not reset review state. Check storage and try again.",m_sqlDetail);return;}
    if (recovering) clearHistoryForRecovery(QStringLiteral("Damaged schedules were explicitly reset. Their original values are retained in a recovery copy."),m_lastRecoveryBackup);
    m_queue.clear();m_sessionOrder.clear();m_queueCache.clear();m_sessionGrades.clear();
    m_selectedVariant.clear();m_sessionTotal=0;m_sessionActive=false;m_queueDirty=true;
    // End the session only after the reset is durable, even if refreshing later fails.
    emit reviewQueueReady({},0,true,{},false,{});
    if (publishSnapshot()) done(QStringLiteral("Reset %1 source card(s). All variants are new and due now.").arg(count));
#endif
}
void DatabaseWorker::cancelStartupReviewCandidate()
{
    assertThread();
    ++m_startupScanGeneration;
    m_startupScan.reset();
}
void DatabaseWorker::finishStartupReviewScan(const quint64 generation,const QVariantMap &card,const QVariantMap &error)
{
    assertThread();
    if (!m_startupScan || m_startupScan->generation!=generation) return;
    m_startupScan.reset();
    emit startupReviewCandidateReady(card,error);
}
void DatabaseWorker::queueStartupReviewSlice(const quint64 generation)
{
    assertThread();
    QTimer::singleShot(0,this,[this,generation] {processStartupReviewSlice(generation);});
}
void DatabaseWorker::selectStartupReviewCandidate()
{
    assertThread();
    cancelStartupReviewCandidate();
    if (!m_db.isOpen()) {
        emit startupReviewCandidateReady({},{{"code","STARTUP_REVIEW_UNAVAILABLE"},
            {"message","Storage is unavailable for the optional quick review. Check the data directory and restart."},
            {"detail",QString()}});
        return;
    }
    if (m_sessionActive || m_startupSession) {emit startupReviewCandidateReady({},{});return;}
    m_startupScan.emplace();
    m_startupScan->generation=m_startupScanGeneration;
    m_startupScan->now=model::nowUtc();
    queueStartupReviewSlice(m_startupScanGeneration);
}
void DatabaseWorker::processStartupReviewSlice(const quint64 generation)
{
    assertThread();
    if (!m_startupScan || m_startupScan->generation!=generation || generation!=m_startupScanGeneration) return;
    if (!m_db.isOpen() || m_sessionActive || m_startupSession) {cancelStartupReviewCandidate();return;}
    m_sqlDetail.clear();
    struct StartupScan &scan=*m_startupScan;
    const auto failScan=[this,generation](const QString &code,const QString &message,const QString &detail) {
        finishStartupReviewScan(generation,{},{{"code",code},{"message",message},{"detail",detail}});
    };
    constexpr int workBudgetMs=5;
    constexpr int dueBatch=64;
    constexpr int reviewBatch=256;
    constexpr int cpuBatch=512;
    constexpr int selectionBatch=8;
    static_assert(dueBatch>0 && reviewBatch>0 && cpuBatch>0 && selectionBatch>0 && workBudgetMs>0);
    QElapsedTimer elapsed;
    elapsed.start();

    if (scan.phase==StartupScan::Phase::Due) {
        QSqlQuery due(m_db);
        due.setForwardOnly(true);
        const QString sql=QStringLiteral("SELECT v.id,v.card_id,v.variant_key,v.due,v.review_count,v.stability,v.difficulty,"
                         "n.deck_id,n.kind,n.front,n.back,n.tags,n.point_count,n.narrations,n.narration_fingerprints,"
                         "d.name,v.graduated AS graduated,v.rowid AS startup_rowid,cooldown.value FROM review_variants v "
                         "LEFT JOIN notes n ON n.id=v.card_id LEFT JOIN decks d ON d.id=n.deck_id "
                         "LEFT JOIN metadata cooldown ON cooldown.key='startup_review_shown:'||v.card_id")
            + (scan.hasCursor?QStringLiteral(" WHERE v.rowid>?"):QString{})
            + QStringLiteral(" ORDER BY v.rowid LIMIT ?");
        if (!due.prepare(sql)) {
            failScan(QStringLiteral("STARTUP_REVIEW_LOAD"),QStringLiteral("Could not prepare the quick review queue. Continue with a regular review or inspect storage."),due.lastError().text());return;
        }
        if (scan.hasCursor) due.addBindValue(scan.rowCursor);
        due.addBindValue(dueBatch);
        if (!due.exec()) {failScan(QStringLiteral("STARTUP_REVIEW_LOAD"),QStringLiteral("Could not read the quick review queue. Continue with a regular review or inspect storage."),due.lastError().text());return;}
        int examined=0;
        bool yielded=false;
        while (due.next()) {
            scan.rowCursor=due.value(17).toLongLong();
            scan.hasCursor=true;
            ++examined;
            QString storageError;
            const struct model::Variant schedule=readVariant(due,&storageError);
            bool textFieldsValid=true;
            for (const int column:{7,8,9,10,11,13,14,15})
                if (due.value(column).isNull() || due.value(column).metaType().id()!=QMetaType::QString) textFieldsValid=false;
            const QVariant pointsValue=due.value(12);
            const int pointsType=pointsValue.metaType().id();
            const double points=pointsValue.toDouble();
            const bool validPoints=(pointsType==QMetaType::LongLong || pointsType==QMetaType::Int || pointsType==QMetaType::Double)
                && std::isfinite(points) && std::floor(points)==points && points>=1 && points<=1000;
            const qint64 nowMs=QDateTime::fromString(scan.now,Qt::ISODateWithMs).toMSecsSinceEpoch();
            bool cooledDown=true;
            if (!due.value(18).isNull()) {
                const auto shown=startupReviewTimestamp(due.value(18),nowMs);
                if (!shown) {
                    cooledDown=false;
                    if (!scan.invalidCooldownCards.contains(schedule.cardId)) {
                        scan.invalidCooldownCards.insert(schedule.cardId);
                        if (m_collectionIssues.size()<100) {
                            m_collectionIssues.append(QVariantMap{{"code","STARTUP_REVIEW_COOLDOWN"},
                                {"message","A card's Review Pit Stop cooldown needs repair. Other cards remain available for review."},
                                {"detail",QStringLiteral("Card %1 has an invalid or far-future cooldown timestamp in metadata key startup_review_shown:%1. Check the system clock and preserve a database recovery copy before repair.").arg(schedule.cardId)}});
                            emit collectionIssuesReady(m_collectionIssues);
                        }
                    }
                } else {
                    cooledDown=*shown<=nowMs-startupReviewCooldownMs;
                }
            }
            if (storageError.isEmpty() && schedule.due<=scan.now && textFieldsValid && validPoints && cooledDown
                && !m_cardLoadErrors.contains(schedule.cardId)) {
                QString metadataError;
                const QVariantMap narrations=readMetadata(due.value(13).toString(),QStringLiteral("narrations"),&metadataError);
                const QVariantMap fingerprints=readMetadata(due.value(14).toString(),QStringLiteral("narration fingerprints"),&metadataError);
                const struct model::Card note{schedule.cardId,due.value(7).toString(),due.value(8).toString(),
                    due.value(9).toString(),due.value(10).toString(),due.value(11).toString(),due.value(12).toInt(),narrations,fingerprints};
                const auto keys=model::variantKeys(note);
                if (metadataError.isEmpty() && model::cardFromMap(model::toMap(note))
                    && model::variantFromMap(model::toMap(schedule)) && keys && keys->contains(schedule.key)) {
                    const QVariantMap card=reviewCard(note,schedule,due.value(15).toString());
                    const struct StartupCandidate candidate{schedule.id,schedule.cardId,startupReviewFingerprint(card),
                        model::readingBudget(card.value("question").toString()),0,0,false};
                    const auto position=scan.positions.constFind(schedule.id);
                    if (position==scan.positions.cend()) {
                        scan.positions.insert(schedule.id,scan.candidates.size());
                        scan.candidates.append(candidate);
                    } else {
                        scan.candidates[*position]=candidate;
                    }
                }
            }
            if (examined>=dueBatch || elapsed.elapsed()>=workBudgetMs) {yielded=true;break;}
        }
        if (due.lastError().isValid()) {failScan(QStringLiteral("STARTUP_REVIEW_LOAD"),QStringLiteral("Could not finish reading the quick review queue. Continue with a regular review or inspect storage."),due.lastError().text());return;}
        due.finish();
        if (!yielded) {
            if (scan.candidates.isEmpty()) {finishStartupReviewScan(generation,{},{});return;}
            scan.activeCount=scan.candidates.size();
            scan.rowCursor=0;
            scan.hasCursor=false;
            scan.phase=StartupScan::Phase::History;
        }
        queueStartupReviewSlice(generation);
        return;
    }
    if (scan.phase==StartupScan::Phase::History) {
        QSqlQuery reviews(m_db);
        reviews.setForwardOnly(true);
        const QString sql=QStringLiteral("SELECT id,card_id,variant_id,deck_name,grade,recall_fraction,response_seconds,"
                             "reviewed_at,due,session_id,session_deck_name,session_started_at,rowid FROM reviews")
            + (scan.hasCursor?QStringLiteral(" WHERE rowid>?"):QString{})
            + QStringLiteral(" ORDER BY rowid LIMIT ?");
        if (!reviews.prepare(sql)) {
            failScan(QStringLiteral("STARTUP_REVIEW_HISTORY"),QStringLiteral("Could not prepare the quick review history. Continue with a regular review or inspect storage."),reviews.lastError().text());return;
        }
        if (scan.hasCursor) reviews.addBindValue(scan.rowCursor);
        reviews.addBindValue(reviewBatch);
        if (!reviews.exec()) {failScan(QStringLiteral("STARTUP_REVIEW_HISTORY"),QStringLiteral("Could not read quick review times. Continue with a regular review or inspect storage."),reviews.lastError().text());return;}
        int examined=0;
        bool yielded=false;
        while (reviews.next()) {
            scan.rowCursor=reviews.value(12).toLongLong();
            scan.hasCursor=true;
            ++examined;
            const QVariant variantId=reviews.value(2);
            const QVariant cardId=reviews.value(1);
            if (!variantId.isNull() && !cardId.isNull()
                && variantId.metaType().id()==QMetaType::QString && cardId.metaType().id()==QMetaType::QString) {
                const auto position=scan.positions.constFind(variantId.toString());
                if (position!=scan.positions.cend()) {
                    struct StartupCandidate &candidate=scan.candidates[*position];
                    if (candidate.cardId==cardId.toString()) {
                        QString storageError;
                        const struct model::Review review=readReview(reviews,&storageError);
                        if (storageError.isEmpty() && review.responseSeconds>0.0 && std::isfinite(review.responseSeconds)) {
                            if (candidate.sampleCount==0) ++scan.historicCount;
                            ++candidate.sampleCount;
                            candidate.meanSeconds+=(review.responseSeconds-candidate.meanSeconds)/static_cast<double>(candidate.sampleCount);
                        }
                    }
                }
            }
            if (examined>=reviewBatch || elapsed.elapsed()>=workBudgetMs) {yielded=true;break;}
        }
        if (reviews.lastError().isValid()) {failScan(QStringLiteral("STARTUP_REVIEW_HISTORY"),QStringLiteral("Could not finish reading quick review times. Continue with a regular review or inspect storage."),reviews.lastError().text());return;}
        reviews.finish();
        if (!yielded) {
            scan.useHistory=scan.historicCount>0;
            const qsizetype eligible=scan.useHistory?scan.historicCount:scan.activeCount;
            assert(eligible>0);
            scan.rankLimit=(eligible+3)/4;
            scan.index=0;
            scan.phase=StartupScan::Phase::Rank;
        }
        queueStartupReviewSlice(generation);
        return;
    }
    if (scan.phase==StartupScan::Phase::Rank) {
        int examined=0;
        while (scan.index<scan.candidates.size()) {
            const struct StartupCandidate &candidate=scan.candidates.at(scan.index++);
            if (!candidate.excluded && (!scan.useHistory || candidate.sampleCount>0)) {
                const double score=scan.useHistory?candidate.meanSeconds:candidate.readingBudget;
                assert(std::isfinite(score) && score>0);
                if (scan.fastest.size()<scan.rankLimit) {
                    scan.fastest.append(score);
                    std::push_heap(scan.fastest.begin(),scan.fastest.end());
                } else if (score<scan.fastest.constFirst()) {
                    std::pop_heap(scan.fastest.begin(),scan.fastest.end());
                    scan.fastest.last()=score;
                    std::push_heap(scan.fastest.begin(),scan.fastest.end());
                }
            }
            if (++examined>=cpuBatch || elapsed.elapsed()>=workBudgetMs) break;
        }
        if (scan.index==scan.candidates.size()) {
            assert(!scan.fastest.isEmpty() && scan.fastest.size()==scan.rankLimit);
            scan.threshold=scan.fastest.constFirst();
            scan.fastest.clear();
            scan.index=0;
            scan.phase=StartupScan::Phase::Pool;
        }
        queueStartupReviewSlice(generation);
        return;
    }
    if (scan.phase==StartupScan::Phase::Pool) {
        int examined=0;
        while (scan.index<scan.candidates.size()) {
            const qsizetype position=scan.index++;
            const struct StartupCandidate &candidate=scan.candidates.at(position);
            if (!candidate.excluded && (!scan.useHistory || candidate.sampleCount>0)) {
                const double score=scan.useHistory?candidate.meanSeconds:candidate.readingBudget;
                if (score<=scan.threshold) scan.pool.append(position);
            }
            if (++examined>=cpuBatch || elapsed.elapsed()>=workBudgetMs) break;
        }
        if (scan.index==scan.candidates.size()) {
            assert(!scan.pool.isEmpty());
            scan.phase=StartupScan::Phase::Pick;
        }
        queueStartupReviewSlice(generation);
        return;
    }
    assert(scan.phase==StartupScan::Phase::Pick);
    int examined=0;
    while (!scan.pool.isEmpty()) {
        const qsizetype selected=static_cast<qsizetype>(QRandomGenerator::global()->bounded(static_cast<quint64>(scan.pool.size())));
        const qsizetype position=scan.pool.at(selected);
        scan.pool[selected]=scan.pool.constLast();
        scan.pool.removeLast();
        struct StartupCandidate &candidate=scan.candidates[position];
        assert(!candidate.excluded && scan.activeCount>0);
        const auto card=reviewCardByVariant(candidate.id);
        if (!m_sqlDetail.isEmpty()) {
            failScan(QStringLiteral("STARTUP_REVIEW_LOAD"),QStringLiteral("Could not load the selected quick review card. Continue with a regular review or inspect storage."),m_sqlDetail);return;
        }
        if (card && card->value("due").toString()<=model::nowUtc()
            && startupReviewFingerprint(*card)==candidate.fingerprint) {
            QVariantMap result=*card;
            if (candidate.sampleCount>0) {
                result.insert("averageResponseSeconds",candidate.meanSeconds);
                result.insert("historySampleCount",QVariant::fromValue(static_cast<qlonglong>(candidate.sampleCount)));
            }
            finishStartupReviewScan(generation,result,{});
            return;
        }
        candidate.excluded=true;
        --scan.activeCount;
        if (candidate.sampleCount>0) {assert(scan.historicCount>0);--scan.historicCount;}
        if (++examined>=selectionBatch || elapsed.elapsed()>=workBudgetMs) break;
    }
    if (scan.pool.isEmpty()) {
        if (scan.activeCount==0) {finishStartupReviewScan(generation,{},{});return;}
        scan.useHistory=scan.historicCount>0;
        const qsizetype eligible=scan.useHistory?scan.historicCount:scan.activeCount;
        scan.rankLimit=(eligible+3)/4;
        scan.index=0;
        scan.fastest.clear();
        scan.phase=StartupScan::Phase::Rank;
    }
    queueStartupReviewSlice(generation);
}

void DatabaseWorker::beginStartupReview(const QVariantMap &expected)
{
    cancelStartupReviewCandidate();
    if (!ready()) return;
    if (!m_historyReady) {fail("UNDO_CAPTURE_UNAVAILABLE","Undo history is unavailable. Restart before starting a quick review.");return;}
    if (m_sessionActive || m_startupSession) {fail("STARTUP_REVIEW_ACTIVE","A review is already active. Finish it before starting a quick review.");return;}
    const QString id=expected.value("variantId").toString();
    if (!model::validUuid(id)) {fail("STARTUP_REVIEW_STALE","The proposed quick review is no longer available. Start a regular review.");return;}
    const auto current=reviewCardByVariant(id);
    if (!m_sqlDetail.isEmpty()) {fail("STARTUP_REVIEW_LOAD","Could not load the proposed review card. Start a regular review or inspect storage.",m_sqlDetail);return;}
    static const QStringList fields{QStringLiteral("id"),QStringLiteral("deckId"),QStringLiteral("kind"),
        QStringLiteral("front"),QStringLiteral("back"),QStringLiteral("pointCount"),QStringLiteral("variantId"),
        QStringLiteral("variantKey"),QStringLiteral("due"),QStringLiteral("reviewCount"),QStringLiteral("stability"),
        QStringLiteral("difficulty"),QStringLiteral("graduated"),QStringLiteral("question"),QStringLiteral("answer")};
    const bool changed=!current || std::any_of(fields.cbegin(),fields.cend(),[&](const QString &field) {
        return expected.value(field)!=current->value(field);
    });
    if (changed || current->value("due").toString()>model::nowUtc()) {
        fail("STARTUP_REVIEW_STALE","The proposed card changed or is no longer due. Start a regular review instead.");return;
    }
    const QString cardId=current->value("id").toString();
    assert(model::validUuid(cardId));
    const QString cooldownKey=QStringLiteral("startup_review_shown:")+cardId;
    bool recent=false;
    bool invalidCooldown=false;
    if (!mutate([&] {
        const qint64 nowMs=QDateTime::currentMSecsSinceEpoch();
        QSqlQuery cooldown(m_db);
        if (!cooldown.prepare(QStringLiteral("SELECT value FROM metadata WHERE key=?"))) {
            m_sqlDetail=cooldown.lastError().text();return false;
        }
        cooldown.addBindValue(cooldownKey);
        if (!cooldown.exec()) {m_sqlDetail=cooldown.lastError().text();return false;}
        if (cooldown.next()) {
            const auto shown=startupReviewTimestamp(cooldown.value(0),nowMs);
            if (!shown) {
                invalidCooldown=true;
                m_sqlDetail=QStringLiteral("Card %1 has an invalid or far-future cooldown timestamp in metadata key %2. Check the system clock and preserve a database recovery copy before repair.").arg(cardId,cooldownKey);
                return false;
            }
            if (*shown>nowMs-startupReviewCooldownMs) {recent=true;return false;}
        }
        if (cooldown.lastError().isValid()) {m_sqlDetail=cooldown.lastError().text();return false;}
        cooldown.finish();
        return execute(QStringLiteral("INSERT INTO metadata(key,value) VALUES(?,?) ON CONFLICT(key) DO UPDATE SET value=excluded.value"),
                       {cooldownKey,QString::number(nowMs)});
    })) {
        if (recent) fail("STARTUP_REVIEW_COOLDOWN","This card was recently shown in Review Pit Stop. Start a regular review instead.");
        else if (invalidCooldown) fail("STARTUP_REVIEW_COOLDOWN","This card's Review Pit Stop cooldown needs repair. Check the system clock or use a regular review.",m_sqlDetail);
        else fail("STARTUP_REVIEW_COOLDOWN_STORAGE","Could not save the Review Pit Stop cooldown. Check storage and start a regular review instead.",m_sqlDetail);
        return;
    }
    m_sessionId=model::uuid();m_sessionStartedAt=model::nowUtc();
    m_sessionDeckName=current->value("deckName").toString();
    m_queue={id};m_sessionOrder={id};m_queueCache.clear();m_sessionGrades.clear();
    m_selectedVariant=id;m_queueDirty=true;m_sessionTotal=1;m_sessionActive=true;m_startupSession=true;
    if (!publishQueue(true)) {
        m_queue.clear();m_sessionOrder.clear();m_queueCache.clear();m_sessionGrades.clear();
        m_selectedVariant.clear();m_sessionTotal=0;m_sessionActive=false;m_startupSession=false;m_queueDirty=true;
        return;
    }
    if (m_sessionOrder.isEmpty()) {
        m_startupSession=false;
        fail("STARTUP_REVIEW_STALE","The proposed card is no longer available for review. Start a regular review instead.");return;
    }
    done("Quick review started");
}

void DatabaseWorker::endStartupReview()
{
    if (!ready()) return;
    if (!m_startupSession) {fail("STARTUP_REVIEW_INACTIVE","The quick review has already ended. Return to your collection.");return;}
    m_queue.clear();m_sessionOrder.clear();m_queueCache.clear();m_sessionGrades.clear();
    m_sessionActive=false;m_startupSession=false;m_selectedVariant.clear();m_queueDirty=true;
    emit reviewQueueReady({},m_sessionTotal,true,{},false,{});
    done("Quick review ended");
}

void DatabaseWorker::beginReview(const QString &deckId, const QString &order)
{
    cancelStartupReviewCandidate();
    const struct HistoryScope history(this, QStringLiteral("Start review"));
    if (!history) return;
    if (!ready()) return;
    if (order != QStringLiteral("due") && order != QStringLiteral("weakest")) {
        fail("REVIEW_ORDER_INVALID", "Choose a supported review order.");
        return;
    }
    if (!publishSnapshot()) return;
    if (!deckId.isEmpty() && (!model::validUuid(deckId)||!deck(deckId))) {fail("DECK_NOT_FOUND","Choose an existing deck to review.");return;}
    const QString sessionId=model::uuid();
    const QString sessionStartedAt=model::nowUtc();
    QString sessionDeckName=QStringLiteral("All decks");
    if (!deckId.isEmpty()) {
        const auto scoped=deck(deckId);
        if (scoped) sessionDeckName=scoped->name;
    }
    QSqlQuery q(m_db);
    q.prepare("SELECT v.id FROM review_variants v JOIN notes n ON n.id=v.card_id WHERE v.due<=?"+QString(deckId.isEmpty()?"":" AND n.deck_id IN (WITH RECURSIVE descendants(id) AS (SELECT id FROM decks WHERE id=? UNION SELECT d.id FROM decks d JOIN descendants ON d.parent_id=descendants.id) SELECT id FROM descendants)")+" ORDER BY v.due,n.id,v.variant_key");
    q.addBindValue(model::nowUtc());if (!deckId.isEmpty()) q.addBindValue(deckId);
    if (!q.exec()) {fail("REVIEW_START","Could not start review. Refresh and try again.",q.lastError().text());return;}
    m_sessionId=sessionId;m_sessionStartedAt=sessionStartedAt;m_sessionDeckName=sessionDeckName;
    m_queue.clear();m_sessionOrder.clear();m_queueCache.clear();m_sessionGrades.clear();m_selectedVariant.clear();m_queueDirty=true;while (q.next()) { const QString id=q.value(0).toString(); m_queue.append(id); m_sessionOrder.append(id); }
    if (order == QStringLiteral("weakest")) {
        std::stable_sort(m_sessionOrder.begin(),m_sessionOrder.end(),[this](const QString &left,const QString &right) {
            const QVariantMap a=m_weaknessByVariant.value(left), b=m_weaknessByVariant.value(right);
            const bool assessedA=a.value("assessed").toBool(), assessedB=b.value("assessed").toBool();
            const double scoreA=a.value("score").toDouble(), scoreB=b.value("score").toDouble();
            const bool prioritizedA=assessedA&&scoreA>0.0, prioritizedB=assessedB&&scoreB>0.0;
            if (prioritizedA != prioritizedB) return prioritizedA;
            if (scoreA != scoreB) return scoreA > scoreB;
            return false;
        });
        m_queue=m_sessionOrder;
    }
    if (!m_queue.isEmpty()) m_selectedVariant=m_queue.first();
    m_sessionTotal=m_queue.size();m_sessionActive=!m_queue.isEmpty();
    m_historyRuntimeChanged=true;
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
    const struct HistoryScope history(this, QStringLiteral("End review"));
    if (!history) return;
    if (!ready()) return;
    m_queue.clear();m_sessionOrder.clear();m_queueCache.clear();m_sessionGrades.clear();
    m_sessionActive=false;m_selectedVariant.clear();m_queueDirty=true;
    m_historyRuntimeChanged=true;
    emit reviewQueueReady({},m_sessionTotal,true,{},false,{});done("Review ended");
}
void DatabaseWorker::defer()
{
    const struct HistoryScope history(this, QStringLiteral("Defer card"));
    if (!history) return;
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
    m_historyRuntimeChanged=true;
    if (publishQueue(true)) done("Card moved to the queue end");
}
void DatabaseWorker::postpone(const QString &date)
{
    const struct HistoryScope history(this, QStringLiteral("Postpone card"));
    if (!history) return;
    if (!ready()) return;
    const QDate day=QDate::fromString(date,Qt::ISODate);
    if (!day.isValid()||date!=day.toString(Qt::ISODate)||day<=QDate::currentDate()) {fail("INVALID_POSTPONE_DATE","Choose a later date in YYYY-MM-DD format.");return;}
    if (!m_sessionActive||m_queue.isEmpty()) {fail("REVIEW_INACTIVE","Start a review before postponing a card.");return;}
    const qsizetype position=m_queue.indexOf(m_selectedVariant);
    if (position<0) {fail("REVIEW_COMPLETED","This card is already graded. Choose an ungraded card to postpone.");return;}
    const auto existing=variant(m_selectedVariant,true);
    if (!existing) {
        const QString cause=m_sqlDetail;
        if (!publishSnapshot()) return;
        m_queueDirty=true;
        if (publishQueue(true)) {
            if (cause.isEmpty()) fail("CARD_NOT_FOUND","This review card no longer exists. Continue with the next card.");
            else fail("REVIEW_CARD_UNREADABLE","This card's stored schedule could not be read. It is excluded from review; other readable cards remain available.",cause);
        }
        return;
    }
    const auto note=card(existing->cardId);
    if (!note||m_cardLoadErrors.contains(existing->cardId)) {
        const QString cause=m_cardLoadErrors.value(existing->cardId).value("detail").toString();
        if (!publishSnapshot()) return;
        m_queueDirty=true;
        if (publishQueue(true)) fail("REVIEW_CARD_UNREADABLE","Repair this card before postponing its schedule. Other readable cards remain available.",cause);
        return;
    }
    const QDateTime localStart=day.startOfDay(QTimeZone::systemTimeZone());
    if (!localStart.isValid()) {fail("INVALID_POSTPONE_DATE","The selected date has no local start of day. Choose another date.");return;}
    struct model::Variant schedule=*existing;schedule.due=localStart.toUTC().toString(Qt::ISODateWithMs);
    if (!mutate([&]{return upsertVariant(schedule)&&enqueue("variant.upsert",{{"variant",model::toMap(schedule)},{"reset",false}});})) {fail("REVIEW_POSTPONE","Could not postpone this card. Check storage and try again.",m_sqlDetail);return;}
    m_queue.removeAt(position);m_sessionOrder.removeAll(m_selectedVariant);advanceAfterRemoval(position);
    if (publishSnapshot()&&publishQueue(true)) done("Review postponed to "+date);
}
void DatabaseWorker::gradeCard(int grade,double recall,double seconds,const QString &expectedVariant)
{
    const struct HistoryScope history(this, QStringLiteral("Grade card"));
    if (!history) return;
    if (!ready()) return;
    if (grade<0||grade>4||!std::isfinite(recall)||recall<0||recall>1||!std::isfinite(seconds)||seconds<0||seconds>86400) {fail("INVALID_GRADE","Choose a grade from 0 to 4 and a recall fraction between 0 and 1.");return;}
    if (!m_sessionActive||!m_sessionOrder.contains(expectedVariant)||m_selectedVariant!=expectedVariant) {fail("REVIEW_STALE","The review card changed. Read the current card and try again.");return;}
    const auto old=variant(expectedVariant,true);
    const auto note=old?card(old->cardId):std::nullopt;
    const auto sourceDeck=note?deck(note->deckId):std::nullopt;
    if (!m_sqlDetail.isEmpty()) {
        const QString cause=m_sqlDetail;
        if (!publishSnapshot()) return;
        m_queueDirty=true;
        if (publishQueue(false)) fail("REVIEW_CARD_UNREADABLE","This card's stored schedule could not be read. It is excluded from review; other readable cards remain available.",cause);
        return;
    }
    if (!old||!note||!sourceDeck) {m_queueDirty=true;if (publishQueue(false)) fail("CARD_NOT_FOUND","This review card no longer exists. Choose another card.");return;}
    const auto validCard=model::cardFromMap(model::toMap(*note));
    const auto validSchedule=model::variantFromMap(model::toMap(*old));
    const auto keys=model::variantKeys(*note);
    if (!validCard||!validSchedule||!keys||!keys->contains(old->key)||m_cardLoadErrors.contains(note->id)) {
        const QString cause=!validCard?validCard.error():!validSchedule?validSchedule.error():m_cardLoadErrors.contains(note->id)?m_cardLoadErrors.value(note->id).value("detail").toString():QStringLiteral("The stored direction no longer matches the card.");
        if (!publishSnapshot()) return;
        m_queueDirty=true;
        if (publishQueue(false)) fail("REVIEW_CARD_UNREADABLE","This card needs repair and has been removed from this review session. Other cards remain available.",QStringLiteral("Card %1: %2").arg(note->id,cause));
        return;
    }
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
    const struct betterflash::scheduler::ReviewResult result=betterflash::scheduler::schedule(baseline.stability,baseline.difficulty,grade,recall,effectiveSeconds,baseline.reviewCount,baseline.graduated?betterflash::scheduler::Phase::Daily:betterflash::scheduler::Phase::Learning,previous.readingBudget);
    const QString due=QDateTime::fromString(reviewedAt,Qt::ISODateWithMs).addSecs(result.interval.count()).toString(Qt::ISODateWithMs);
    const struct model::Variant schedule{old->id,old->cardId,old->key,due,baseline.reviewCount+1,result.stability,result.difficulty,result.graduated};
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
    if (pendingHistoryEntity(type,id)) return true;
    if (!m_sqlDetail.isEmpty()) return false;
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
    if (!(type==QStringLiteral("deck") && authoritativeOnly) && pendingHistoryEntity(type,id,true)) return true;
    if (!m_sqlDetail.isEmpty()) return false;
    QSqlQuery query(m_db);
    query.prepare("SELECT 1 FROM local_events WHERE echoed=0 AND target_type=? AND target_id=? AND json_extract(event,'$.payload.restore')=1"
                  + QString(authoritativeOnly ? " AND COALESCE(json_extract(event,'$.payload.restoreDependency'),0)=0" : "") + " LIMIT 1");
    query.addBindValue(type);
    query.addBindValue(id);
    if (!query.exec()) { m_sqlDetail=query.lastError().text(); return false; }
    return query.next();
}
bool DatabaseWorker::applyDeckDelete(const QString &id,qint64 seq,bool &applied,bool applyUnscoped)
{
    QSqlQuery scopeQuery(m_db);
    scopeQuery.prepare("SELECT scopes FROM remote_deletion_scopes WHERE seq=?"); scopeQuery.addBindValue(seq);
    if (!scopeQuery.exec()) { m_sqlDetail=scopeQuery.lastError().text(); return false; }
    const QVariantMap scopes=scopeQuery.next()?QJsonDocument::fromJson(scopeQuery.value(0).toString().toUtf8()).object().toVariantMap():QVariantMap{};
    const bool authoritative=scopes.contains(id);
    const QVariantMap scope=scopes.value(id).toMap();
    if (!applyUnscoped&&!authoritative) return true;
    if (m_allowHierarchyRepair && !authoritative) {
        if (m_hierarchyPageIncomplete) { applied=false;return true; }
        QSqlQuery pending(m_db);
        if (!pending.exec("SELECT EXISTS(SELECT 1 FROM local_events WHERE echoed=0 AND "
                          "(target_type='deck' OR (json_extract(event,'$.type')='history.apply' AND EXISTS "
                          "(SELECT 1 FROM json_each(local_events.event,'$.payload.changes') "
                          "WHERE json_extract(value,'$.entity')='deck'))))") || !pending.next()) {
            m_sqlDetail=pending.lastError().text();return false;
        }
        // Server-ordered hierarchy writes determine which local folders belong
        // to the deleted subtree before its contents can be removed.
        if (pending.value(0).toBool()) { applied=false;return true; }
    }
    QHash<QString,QString> parents;
    if (authoritative) {
        const QVariantMap decks=scope.value("decks").toMap();
        for (auto it=decks.cbegin();it!=decks.cend();++it) parents.insert(it.key(),it.value().toString());
    } else {
        QSqlQuery hierarchy(m_db);
        hierarchy.prepare("WITH RECURSIVE subtree(id) AS (SELECT id FROM decks WHERE id=? UNION SELECT d.id FROM decks d JOIN subtree s ON d.parent_id=s.id) SELECT d.id,d.parent_id FROM decks d JOIN subtree s ON s.id=d.id");
        hierarchy.addBindValue(id);
        if (!hierarchy.exec()) { m_sqlDetail=hierarchy.lastError().text(); return false; }
        while (hierarchy.next()) {
            if (parents.size() >= model::maxCollectionRecords) { m_sqlDetail="The deleted deck subtree exceeds its record limit."; return false; }
            parents.insert(hierarchy.value(0).toString(),hierarchy.value(1).toString());
        }
        if (hierarchy.lastError().isValid()) { m_sqlDetail=hierarchy.lastError().text(); return false; }
    }
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
    QMultiHash<QString,QString> scopedCardsByDeck;
    if (authoritative) {
        const QVariantMap scopedCards=scope.value("cards").toMap();
        for (auto it=scopedCards.cbegin();it!=scopedCards.cend();++it) scopedCardsByDeck.insert(it.value().toString(),it.key());
    }
    for (const QString &deckId:ordered) {
        QStringList cardIds;
        if (authoritative) {
            cardIds=scopedCardsByDeck.values(deckId);
        } else {
            QSqlQuery query(m_db);query.prepare("SELECT id FROM notes WHERE deck_id=?");query.addBindValue(deckId);
            if (!query.exec()) { m_sqlDetail=query.lastError().text(); return false; }
            while (query.next()) cardIds.append(query.value(0).toString());
            if (query.lastError().isValid()) { m_sqlDetail=query.lastError().text(); return false; }
        }
        for (const QString &cardId:cardIds) {
            if (cards.size() >= model::maxCollectionRecords) { m_sqlDetail="The deleted deck's cards exceed their record limit."; return false; }
            const bool pending=pendingRestore("card",cardId);
            const bool protect=protectedScopes.contains(deckId)||pending||seq<entitySequence("restore-card",cardId);
            if (!m_sqlDetail.isEmpty()) return false;
            if (pending||pendingScopes.contains(deckId)) applied=false;
            const auto currentCard=protect&&authoritative?card(cardId):std::optional<struct model::Card>{};
            const bool remainsInDeletedDeck=!currentCard||currentCard->deckId==deckId;
            if (protect&&remainsInDeletedDeck&&!protectedDecks.contains(deckId)) {
                if (authoritative) {applied=false;return true;}
                m_sqlDetail="A restored card was moved into a concurrently deleted deck. Move it back to the restored deck before syncing again.";return false;
            }
            cards.append(DeletedCard{cardId,deckId,protect});
        }
    }
    for (const QString &deckId:protectedDecks) {
        const auto currentDeck=authoritative?deck(deckId):std::optional<struct model::Deck>{};
        if (!m_sqlDetail.isEmpty()) return false;
        const QString parent=currentDeck?currentDeck->parentId:parents.value(deckId);
        if (parents.contains(parent)&&!protectedDecks.contains(parent)) {
            if (authoritative) {applied=false;return true;}
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
    if (type==QStringLiteral("history.apply")) return historyEventEffects(payload.value("changes").toList(),seq,executeEffects,applied,event.value("createdAt").toString());
    if (type==QStringLiteral("deck.delete")) {
        const QString id=payload.value("id").toString();
        return applyDeckDelete(id,seq,applied,executeEffects)
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
        const QVariantMap incomingDeck=object(payload,"deck");
        struct model::Deck record=*model::deckFromMap(incomingDeck);
        if (!incomingDeck.contains("tagVocabulary")) {
            const auto current=deck(record.id);
            if (!m_sqlDetail.isEmpty()) return false;
            if (current) { record.tagVocabulary=current->tagVocabulary; record.tagVocabularyError=current->tagVocabularyError; }
        }
        const bool restore = payload.value("restore").toBool();
        const bool dependency = payload.value("restoreDependency").toBool();
        const bool containerOnly = dependency || payload.value("restoreContainerOnly").toBool();
        if (!executeEffects) {
            if (restore && !recordSequence(containerOnly ? "restore-container" : "restore-deck",record.id,seq)) return false;
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
            if (restore || (m_allowHierarchyRepair && !m_finalizingHierarchyOrphans)) { applied=false; return m_sqlDetail.isEmpty(); }
            if (m_allowHierarchyRepair)
                return applyDeckDelete(record.id,seq,applied) && (!applied || recordSequence("deck",record.id,seq));
            return tombstone("deck",record.id,seq)&&recordSequence("deck",record.id,seq);
        }
        if (!validDeckParent(record.id,record.parentId)) {
            if (!m_allowHierarchyRepair && !record.parentId.isEmpty()&&deck(record.parentId)) {m_sqlDetail="Deck hierarchy contains a cycle.";return false;}
            applied=false;return m_sqlDetail.isEmpty();
        }
        return upsertDeck(record)
            && (!restore || execute("DELETE FROM tombstones WHERE entity_type='deck' AND entity_id=?",{record.id}))
            && (!restore || recordSequence(containerOnly ? "restore-container" : "restore-deck",record.id,seq))
            && recordSequence("deck",record.id,seq);
    }
    if (type==QStringLiteral("card.upsert")) {
        const QVariantMap incomingCardMap = object(payload,"card");
        const auto parsedRecord = model::cardFromMap(incomingCardMap);
        if (!parsedRecord) { m_sqlDetail=parsedRecord.error(); return false; }
        struct model::Card record=*parsedRecord;
        const auto stored = card(record.id);
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
        if (stored) {
            if (!stored->createdAt.isEmpty()) record.createdAt=stored->createdAt;
            if (!incomingCardMap.contains(QStringLiteral("modifiedAt")) || record.modifiedAt.isEmpty()) {
                const auto eventTime=model::utcInstant(event.value(QStringLiteral("createdAt")).toString());
                if (!eventTime) { m_sqlDetail=eventTime.error(); return false; }
                record.modifiedAt=model::sameCardContent(record,*stored)?stored->modifiedAt:*eventTime;
            }
        }
        if (!record.modifiedAt.isEmpty()) {
            if (stored && !stored->modifiedAt.isEmpty()) record.modifiedAt=std::max(record.modifiedAt,stored->modifiedAt);
            if (!record.createdAt.isEmpty()) record.modifiedAt=std::max(record.modifiedAt,record.createdAt);
        }
        QList<struct model::Variant> schedules;
        for (const QVariant &value : payload.value("variants").toList()) schedules.append(*model::variantFromMap(value.toMap()));
        const bool restore = payload.value("restore").toBool();
        if (!executeEffects) {
            if (!recordSequence("card",record.id,seq)) return false;
            for (const QVariant &id:payload.value("freshVariantIds").toList())
                if (!recordSequence("variant",id.toString(),seq)||!recordSequence("variant-barrier",id.toString(),seq)) return false;
            if (restore) {
                if (!recordSequence("restore-card",record.id,seq)) return false;
            } else {
                // Card events carry schedules as creation context. Existing schedules
                // are owned by review events and standalone variant updates.
                for (const struct model::Variant &v : schedules) {
                    const auto existing=variant(v.id);
                    if (!m_sqlDetail.isEmpty()) return false;
                    if (existing) continue;
                    if (!recordSequence("variant",v.id,seq)) return false;
                }
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
            return eraseCard(record.id,seq);
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
        const auto parsedKeys=model::variantKeys(*current);
        if (!parsedKeys) {m_sqlDetail=QStringLiteral("Card %1 must be repaired before applying its remote schedules: %2").arg(current->id,parsedKeys.error());return false;}
        const QStringList keys=*parsedKeys;
        QSet<QString> freshVariantIds;
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
                if (!upsertVariant(v)||!recordSequence("variant",v.id,seq)||!recordSequence("variant-barrier",v.id,seq)) return false;
                continue;
            }
            const auto existing=variant(v.id);
            if (!m_sqlDetail.isEmpty()) return false;
            const bool fresh=freshVariantIds.contains(v.id);
            if (existing && !fresh) continue;
            if (fresh && seq<=entitySequence("variant",v.id)) continue;
            if (pendingEntity("variant",v.id)) {applied=false;continue;}
            if (!upsertVariant(v)||!recordSequence("variant",v.id,seq)||!recordSequence("variant-barrier",v.id,seq)) return false;
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
    if (!executeEffects) return (type!=QStringLiteral("variant.upsert")||recordSequence("variant-barrier",schedule.id,seq))&&recordSequence("variant",schedule.id,seq)
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
    const auto sourceKeys=model::variantKeys(*source);
    if (!sourceKeys) {m_sqlDetail=QStringLiteral("Card %1 must be repaired before applying its remote review: %2").arg(source->id,sourceKeys.error());return false;}
    if (!sourceKeys->contains(schedule.key)) return true;
    const auto persistedSchedule=variant(schedule.id);
    const QString owner=reviewOwner(schedule.id);
    const qint64 variantSequence=entitySequence("variant",schedule.id);
    if (!m_sqlDetail.isEmpty()) return false;
    if (seq<=entitySequence("variant-barrier",schedule.id)) return m_sqlDetail.isEmpty();
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
    return upsertVariant(schedule,record?record->id:QString())&&recordSequence("variant",schedule.id,seq)
        &&(type!=QStringLiteral("variant.upsert")||recordSequence("variant-barrier",schedule.id,seq));
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
        if (!applied) return true;
        return execute("UPDATE seen_events SET seq=? WHERE id=?",{seq,event.value("id")})
            &&execute("UPDATE local_events SET echoed=1 WHERE id=?",{event.value("id")});
    }
    if (!eventEffects(event,seq,true,applied)) return false;
    return !applied||execute("INSERT INTO seen_events(id,seq) VALUES(?,?)",{event.value("id"),seq});
}
bool DatabaseWorker::retryRemoteEvents()
{
    assert(!m_finalizingHierarchyOrphans);
    const auto resetOrphanPolicy=qScopeGuard([this] { m_finalizingHierarchyOrphans=false; });
    QSqlQuery q(m_db);
    if (!q.exec("SELECT seq,event FROM remote_pending ORDER BY seq")) {m_sqlDetail=q.lastError().text();return false;}
    QList<QPair<qint64,QVariantMap>> events;
    while (q.next()) events.append({q.value(0).toLongLong(),QJsonDocument::fromJson(q.value(1).toString().toUtf8()).object().toVariantMap()});
    if (!m_allowHierarchyRepair) {
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
            if (colors.value(id)==1) return false;
            if (colors.value(id)==2) return true;
            colors[id]=1;
            if (!acyclic(parents.value(id))) return false;
            colors[id]=2;
            return true;
        };
        for (const QString &id : parents.keys()) if (!acyclic(id)) {m_sqlDetail="Deck hierarchy contains a cycle.";return false;}
    }
    const auto hierarchyDeletion=[](const QVariantMap &event) {
        if (event.value("type")==QStringLiteral("deck.delete")) return true;
        if (event.value("type")!=QStringLiteral("history.apply")) return false;
        for (const QVariant &value:object(event,"payload").value("changes").toList()) {
            const QVariantMap change=value.toMap();
            if (change.value("entity")==QStringLiteral("deck") && change.value("after").toMap().isEmpty()) return true;
        }
        return false;
    };
    bool deletionsPhase=!m_allowHierarchyRepair;
    bool progressed=false;
    do {
        progressed=false;
        for (auto it=events.begin();it!=events.end();) {
            if (!deletionsPhase && hierarchyDeletion(it->second)) { ++it;continue; }
            bool applied=false;
            if (!applyRemoteEvent(it->second,it->first,applied)) return false;
            if (applied) {
                if (!execute("DELETE FROM remote_pending WHERE seq=?",{it->first})
                    ||!execute("DELETE FROM remote_deletion_scopes WHERE seq=?",{it->first})) return false;
                it=events.erase(it);progressed=true;
            } else ++it;
        }
        if (m_allowHierarchyRepair) {
            if (progressed) { deletionsPhase=false; m_finalizingHierarchyOrphans=false; }
            else if (!deletionsPhase) { deletionsPhase=true;progressed=true; }
            else if (!m_hierarchyPageIncomplete && !m_finalizingHierarchyOrphans) {
                m_finalizingHierarchyOrphans=true;progressed=true;
            }
        }
    } while (progressed&&!events.isEmpty());
    return true;
}
QVariantMap DatabaseWorker::syncNotices()
{
    QSqlQuery query(m_db);
    if (!query.exec("SELECT value FROM metadata WHERE key='sync_notices'")) { m_sqlDetail=query.lastError().text(); return {}; }
    if (!query.next()) return {{"revision",QString()},{"contentConflictCount",qint64(0)},{"hierarchyResolutionCount",qint64(0)},{"records",QStringList{}}};
    QJsonParseError error;
    const QJsonDocument document=QJsonDocument::fromJson(query.value(0).toString().toUtf8(),&error);
    if (error.error!=QJsonParseError::NoError || !document.isObject()) { m_sqlDetail="Stored sync notices are invalid. Preserve the database and inspect its metadata."; return {}; }
    return document.object().toVariantMap();
}
void DatabaseWorker::acknowledgeSyncNotices(const QString &revision)
{
    if (!ready()) return;
    if (!model::validUuid(revision)) {fail("INVALID_SYNC_NOTICE","Refresh the sync notices before marking them read.");return;}
    QVariantMap notices;
    bool changed=false;
    const bool committed=mutate([&] {
        notices=syncNotices();
        if (!m_sqlDetail.isEmpty()) return false;
        if (notices.value("revision").toString()!=revision) {changed=true;return false;}
        notices={{"revision",model::uuid()},{"contentConflictCount",qint64(0)},{"hierarchyResolutionCount",qint64(0)},{"records",QStringList{}}};
        return execute("INSERT INTO metadata(key,value) VALUES('sync_notices',?) ON CONFLICT(key) DO UPDATE SET value=excluded.value",{json(notices)});
    });
    if (!committed) {
        if (changed) fail("SYNC_NOTICE_CHANGED","New sync notices arrived. Review them before marking them read.");
        else fail("SYNC_NOTICE_STORAGE","Could not mark sync notices read. Check storage and try again.",m_sqlDetail);
        return;
    }
    emit syncNoticesChanged(notices);
    done("Sync notices marked read");
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
        if (!payload.isObject()) {reject("SYNC_STORAGE","A stored sync payload is invalid. Preserve the database and repair the pending event before syncing.",QStringLiteral("sync_outbox event %1 has a payload that is not a JSON object.").arg(q.value(0).toString().left(128)));return;}
        const QVariantMap event{{"id",q.value(0)},{"deviceId",q.value(1)},{"type",q.value(2)},{"payload",payload.object().toVariantMap()},{"createdAt",q.value(4)}};
        const qsizetype size=json(event).toUtf8().size();
        if (size>model::maxSyncEventBytes) {reject("SYNC_EVENT_LIMIT","A stored change exceeds the sync limit. Shorten the affected card content before syncing.");return;}
        if (events.size()>=100||bytes+size+1>model::maxSyncBatchBytes) {hasMoreLocal=true;break;}
        const auto valid=betterflash::protocol::validateEvent(event);
        if (!valid) {reject("SYNC_STORAGE","A stored sync change is invalid. Preserve the database and inspect details.",valid.error());return;}
        if (event.value("type")==QStringLiteral("card.upsert") || event.value("type")==QStringLiteral("history.apply")) {
            QStringList names;
            QVariantList cardMaps;
            const QVariantMap eventPayload=object(event,"payload");
            if (event.value("type")==QStringLiteral("card.upsert")) cardMaps.append(object(eventPayload,"card"));
            else for (const QVariant &value:eventPayload.value("changes").toList()) {
                const QVariantMap change=value.toMap();
                if (change.value("entity")!=QStringLiteral("card")) continue;
                for (const QString &side:{QStringLiteral("after")})
                    if (change.value(side).metaType().id()==QMetaType::QVariantMap) cardMaps.append(change.value(side));
            }
            for (const QVariant &value:cardMaps) {
                const auto note=model::cardFromMap(value.toMap());
                if (!note) {reject("SYNC_STORAGE","A stored card change is invalid. Preserve the database and inspect details.",note.error());return;}
                const auto references=model::cardMediaReferences(*note);
                if (!references) {reject("SYNC_MEDIA","A stored card has invalid image references. Preserve the database and inspect details.",references.error());return;}
                for (const QString &name:*references) if (!names.contains(name)) names.append(name);
            }
            QSet<QString> combined=mediaNames;
            QStringList additional;
            for (const QString &name:names) if (!combined.contains(name)) {combined.insert(name);additional.append(name);}
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
    const QVariantMap notices=syncNotices();
    if (!m_sqlDetail.isEmpty()) {reject("SYNC_STORAGE","Could not read retained sync notices. Preserve the database and inspect details.",m_sqlDetail);return;}
    emit syncBatchReady({{"syncNotices",notices},{"deviceId",m_deviceId},{"cursor",m_cursor},{"events",events},{"hasMoreLocal",hasMoreLocal},
                         {"requestId",requestId},{"collectionId",m_collectionId}});
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
    const QString responseCollectionId=response.value(QStringLiteral("collectionId")).toString();
    if (response.contains(QStringLiteral("collectionId"))
        && (response.value(QStringLiteral("collectionId")).metaType().id() != QMetaType::QString
            || !model::validUuid(responseCollectionId))) {
        reject("The sync response contains an invalid collection identifier.");return;
    }
    if (!m_collectionId.isEmpty() && responseCollectionId != m_collectionId) {
        reject("The sync response belongs to a different collection.");return;
    }
    const QVariantList events=response.value("events").toList();
    const qint64 cursor=response.value("cursor").toLongLong();
    QSet<QString> acceptedIds;for (const QVariant &id:response.value("acceptedIds").toList()) acceptedIds.insert(id.toString());
    if (!publishSnapshot()) {appliedSignal(false,false);return;}
    const bool recovering=!m_cardLoadErrors.isEmpty()||!m_cardDateErrors.isEmpty()||m_hasUnreadableHistory||m_hasOrphanSchedules||std::any_of(m_collectionIssues.cbegin(),m_collectionIssues.cend(),[](const QVariant &entry) {
        const QString code=entry.toMap().value("code").toString();
        return code==QStringLiteral("REVIEW_RECORD")||code==QStringLiteral("SYNC_STORAGE");
    });
    if (recovering&&!createRecoveryBackup()) {fail("SYNC_RECOVERY_BACKUP","Could not preserve unreadable local records before applying sync. No sync changes were applied.",m_sqlDetail);appliedSignal(false,false);return;}
    bool hasPendingOutbox=false;
    bool unresolvedDependencies=false;
    QVariantMap notices;
    assert(!m_allowHierarchyRepair);
    m_allowHierarchyRepair = !responseCollectionId.isEmpty();
    m_hierarchyPageIncomplete = response.value("hasMore").toBool();
    const auto resetHierarchyPolicy = qScopeGuard([this] { m_allowHierarchyRepair = false; m_hierarchyPageIncomplete = false; });
    m_syncReviewConflicts.clear();
    m_syncHistoryConflicts.clear();
    const bool ok=mutate([&] {
        for (const QVariant &value : events) {
            const QVariantMap wrapper=value.toMap();
            const QVariantMap event=object(wrapper,"event");
            if (wrapper.contains("deletionScopes") && !execute("INSERT OR REPLACE INTO remote_deletion_scopes(seq,scopes) VALUES(?,?)",{wrapper.value("seq"),json(wrapper.value("deletionScopes").toMap())})) return false;
            // Retryable dependencies remain durable even while the download cursor advances.
            if (!execute("INSERT INTO remote_pending(seq,event_id,event) VALUES(?,?,?) ON CONFLICT(seq) DO UPDATE SET event=CASE WHEN event_id=excluded.event_id THEN event ELSE NULL END",{wrapper.value("seq"),event.value("id"),json(event)})) return false;
        }
        for (const QString &id : acceptedIds) if (!execute("DELETE FROM sync_outbox WHERE id=?",{id})) return false;
        if (!retryRemoteEvents()
            || (!responseCollectionId.isEmpty()
                && !execute("INSERT INTO metadata(key,value) VALUES('sync_collection_id',?) ON CONFLICT(key) DO UPDATE SET value=excluded.value",{responseCollectionId}))
            || !execute("UPDATE metadata SET value=? WHERE key='sync_cursor'",{QString::number(cursor)})) return false;
        QSqlQuery pending(m_db);
        if (!pending.exec("SELECT EXISTS(SELECT 1 FROM sync_outbox)" )||!pending.next()) {m_sqlDetail=pending.lastError().text();return false;}
        hasPendingOutbox=pending.value(0).toBool();
        if (!pending.exec("SELECT EXISTS(SELECT 1 FROM remote_pending)")||!pending.next()) {m_sqlDetail=pending.lastError().text();return false;}
        unresolvedDependencies=pending.value(0).toBool();
        notices=syncNotices();
        if (!m_sqlDetail.isEmpty()) return false;
        bool newNotices=false;
        for (const QString &key:{QStringLiteral("contentConflictCount"),QStringLiteral("hierarchyResolutionCount")}) {
            const qint64 increment=response.value(key,key==QStringLiteral("contentConflictCount")?response.value("contentConflicts").toList().size():0).toLongLong();
            const qint64 retained=notices.value(key).toLongLong();
            if (retained<0 || retained>9007199254740991LL-increment) {m_sqlDetail="Retained sync notice count exceeds its limit.";return false;}
            notices.insert(key,retained+increment);
            newNotices=newNotices||increment>0;
        }
        if (newNotices || (notices.value("revision").toString().isEmpty()
            &&(notices.value("contentConflictCount").toLongLong()>0||notices.value("hierarchyResolutionCount").toLongLong()>0))) notices.insert("revision",model::uuid());
        QStringList records=notices.value("records").toStringList();
        for (const QVariant &value:response.value("contentConflicts").toList()) {
            const QVariantMap conflict=value.toMap();
            const QString record=conflict.value("entity").toString()+QLatin1Char(' ')+conflict.value("id").toString();
            if (records.size()<10&&!records.contains(record)) records.append(record);
        }
        notices.insert("records",records);
        return execute("INSERT INTO metadata(key,value) VALUES('sync_notices',?) ON CONFLICT(key) DO UPDATE SET value=excluded.value",{json(notices)});
    });
    if (!ok) {fail("SYNC_APPLY","Could not apply sync changes. Existing records and pending uploads were preserved.",m_sqlDetail);appliedSignal(false,false);return;}
    if (recovering) clearHistoryForRecovery(QStringLiteral("Sync applied with damaged local records present. Their original stored data is retained in a recovery copy."),m_lastRecoveryBackup);
    m_cursor=cursor;
    if (!responseCollectionId.isEmpty()) m_collectionId=responseCollectionId;
    m_queueDirty=true;
    const bool hasMore=response.value("hasMore").toBool()||hasPendingOutbox;
    emit syncNoticesChanged(notices);
    if (!publishSnapshot()||!publishQueue(false)) {appliedSignal(false,false);return;}
    if (!responseCollectionId.isEmpty()&&!hasMore&&unresolvedDependencies) {
        fail("SYNC_DEPENDENCY","Sync retained remote changes whose dependencies are unavailable. Update every client and the server, then retry; preserve the collection if this continues.");
        appliedSignal(false,false);return;
    }
    if (!m_syncHistoryConflicts.isEmpty()) fail("SYNC_UNDO_CONFLICT","Sync finished with an undo or redo conflict. Changed records were preserved; review them before repeating the action.","Record identifiers: "+m_syncHistoryConflicts.join(", "));
    else if (m_syncReviewConflicts.isEmpty()) done("Sync applied");
    else fail("SYNC_REVIEW_CONFLICT","Sync finished with conflicting review schedules. The latest review results are in History; a newer or explicitly changed schedule was preserved. End this review session before grading these cards again.","Review identifiers: "+m_syncReviewConflicts.join(", "));
    appliedSignal(true,hasMore);
}

void DatabaseWorker::exportCollection(const QString &path)
{
    if (!ready()) return;
    if (path.trimmed().isEmpty()||path.size()>4096||path.contains(QChar::Null)||QFileInfo(path).absoluteFilePath()==QFileInfo(m_path).absoluteFilePath()) {fail("INVALID_EXPORT_PATH","Choose a file path different from the application database.");return;}
    QVariantList decks,cards,variants,history,deleted,checkpoints;
    QList<struct model::Card> notes;
    QSqlQuery q(m_db);
    if (!q.exec("SELECT id,name,description,created_at,parent_id,tag_vocabulary FROM decks ORDER BY id")) {fail("EXPORT_READ","Could not read decks for export.",q.lastError().text());return;}
    while (q.next()) {
        const struct model::Deck savedDeck=readDeck(q);
        if (!savedDeck.tagVocabularyError.isEmpty()) { fail("EXPORT_DAMAGED_DECK_TAGS","Portable export stopped because a deck's tag vocabulary needs repair. Preserve a raw SQLite recovery copy before repairing it.",savedDeck.tagVocabularyError); return; }
        decks.append(model::toMap(savedDeck));
    }
    if (!q.exec("SELECT id,deck_id,kind,front,back,tags,point_count,narrations,narration_fingerprints,created_at,modified_at FROM notes ORDER BY id")) {fail("EXPORT_READ","Could not read cards for export.",q.lastError().text());return;}
    while (q.next()) {
        QString storageError;
        const struct model::Card note=readCard(q,&storageError);
        if (!note.dateMetadataError.isEmpty()) { fail("EXPORT_DAMAGED_CARD_DATES","Portable export stopped because a card's saved dates need repair. Preserve a raw SQLite recovery copy before repairing the dates.",QStringLiteral("Card %1: %2").arg(note.id,note.dateMetadataError)); return; }
        const QVariantMap issue=model::cardReadIssue(note,cardVariants(note.id),storageError);
        if (!issue.isEmpty()) {fail("EXPORT_DAMAGED_CARD","Portable export stopped because a saved card needs repair. No export was written. Preserve a raw SQLite recovery copy before repairing the card.",issue.value("detail").toString());return;}
        notes.append(note);cards.append(model::toMap(note));
    }
    if (!q.exec("SELECT id,card_id,variant_key,due,review_count,stability,difficulty,graduated FROM review_variants ORDER BY id")) {fail("EXPORT_READ","Could not read variants for export.",q.lastError().text());return;}
    while (q.next()) {
        QString storageError;
        const struct model::Variant schedule=readVariant(q,&storageError);
        if (!storageError.isEmpty()) {fail("EXPORT_DAMAGED_SCHEDULE","Portable export stopped because a saved schedule needs repair. No export was written. Preserve a raw SQLite recovery copy before resetting schedules.",storageError);return;}
        variants.append(model::toMap(schedule));
    }
    if (!q.exec("SELECT id,card_id,variant_id,deck_name,grade,recall_fraction,response_seconds,reviewed_at,due,session_id,session_deck_name,session_started_at FROM reviews ORDER BY reviewed_at,id")) {fail("EXPORT_READ","Could not read history for export.",q.lastError().text());return;}
    while (q.next()) {
        QString storageError;
        const struct model::Review record=readReview(q,&storageError);
        if (!storageError.isEmpty()) {fail("EXPORT_DAMAGED_REVIEW","Portable export stopped because a saved review entry needs repair. No export was written. Preserve a raw SQLite recovery copy before repairing history.",storageError);return;}
        history.append(model::toMap(record));
    }
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
    const QJsonObject root{{"version",7},{"media",model::mediaToJson(media)},{"decks",QJsonArray::fromVariantList(decks)},{"cards",QJsonArray::fromVariantList(cards)},
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
    const struct HistoryScope history(this, QStringLiteral("Import collection"));
    if (!history) return;
    if (!ready()) return;
    if (path.trimmed().isEmpty()||path.size()>4096||path.contains(QChar::Null)) {
        fail("IMPORT_READ","Could not open the collection. Choose a readable JSON file.","The import path is empty, too long, or contains a null character.");return;
    }
    const QByteArray encodedPath=QFile::encodeName(path);
    const int descriptor=::open(encodedPath.constData(),O_RDONLY|O_CLOEXEC|O_NONBLOCK);
    if (descriptor<0) {fail("IMPORT_READ","Could not open the collection. Choose a readable JSON file.",QStringLiteral("%1: %2").arg(path,QString::fromLocal8Bit(std::strerror(errno))));return;}
    const auto closeDescriptor=qScopeGuard([descriptor] { (void)::close(descriptor); });
    struct stat info{};
    if (::fstat(descriptor,&info)!=0) {fail("IMPORT_READ","Could not inspect the collection. Choose a readable JSON file.",QStringLiteral("%1: %2").arg(path,QString::fromLocal8Bit(std::strerror(errno))));return;}
    if (!S_ISREG(info.st_mode)) {fail("IMPORT_READ","Choose a regular JSON file for import.",QStringLiteral("%1 is not a regular file.").arg(path));return;}
    if (info.st_size>model::maxBackupBytes) {fail("IMPORT_LIMIT","The collection exceeds the 128 MiB import limit. Choose a smaller backup.",QStringLiteral("%1: %2 bytes.").arg(path).arg(info.st_size));return;}
    QFile file;
    if (!file.open(descriptor,QIODevice::ReadOnly)) {fail("IMPORT_READ","Could not read the collection. Choose a readable JSON file.",QStringLiteral("%1: %2").arg(path,file.errorString()));return;}
    QByteArray bytes;
    constexpr qint64 chunkSize=64*1024;
    while (bytes.size()<=model::maxBackupBytes) {
        const QByteArray chunk=file.read(std::min(chunkSize,model::maxBackupBytes+1-bytes.size()));
        if (file.error()!=QFileDevice::NoError) {fail("IMPORT_READ","Could not read the collection. Choose a readable JSON file.",QStringLiteral("%1: %2").arg(path,file.errorString()));return;}
        if (chunk.isEmpty()) break;
        bytes.append(chunk);
    }
    if (bytes.size()>model::maxBackupBytes) {fail("IMPORT_LIMIT","The collection exceeds the 128 MiB import limit. Choose a smaller backup.",QStringLiteral("%1: more than %2 bytes were read.").arg(path).arg(model::maxBackupBytes));return;}
    QJsonParseError parseError;
    const QJsonDocument document=QJsonDocument::fromJson(bytes,&parseError);
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
        if (isDeleted("card",record.id) || (existing && (!model::sameCardContent(*existing,record)
            || (!existing->createdAt.isEmpty() && !record.createdAt.isEmpty() && existing->createdAt!=record.createdAt)))) {collision("Card "+record.id);return;}
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
    const struct HistoryScope history(this, QStringLiteral("Add example deck"));
    if (!history) return;
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
    add("cloze","[cloze1:Atomicity|transaction property] means a transaction commits together. [cloze2:Durability|transaction property] means it survives restart. A [cloze1:rollback] leaves no partial mutation.","Each distinct cloze group has its own review schedule.","storage, cloze");
    const bool ok=mutate([&] {
        if (!upsertDeck(example)||!enqueue("deck.upsert",{{"deck",model::toMap(example)}})) return false;
        for (struct model::Card note : notes) {
            model::setCardEditDates(note,nullptr,example.createdAt);
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
