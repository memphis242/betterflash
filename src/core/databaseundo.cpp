#include "databaseworker.h"
#include "mediabackup.h"
#include "syncprotocol.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QMap>
#include <QSqlError>
#include <QSqlQuery>
#include <algorithm>
#include <array>
#include <cassert>

namespace model = betterflash::model;
namespace {
QString encoded(const QVariantMap &value)
{
    return QString::fromUtf8(QJsonDocument(QJsonObject::fromVariantMap(value)).toJson(QJsonDocument::Compact));
}
QVariant absent() { return QVariant::fromValue(nullptr); }
bool present(const QVariant &value) { return value.metaType().id() == QMetaType::QVariantMap; }
QString rowKey(const QVariantMap &change)
{
    return change.value("entity").toString() + QLatin1Char('/') + change.value("id").toString();
}
QVariantMap canonical(const QString &entity, const QVariantMap &row)
{
    if (entity == QStringLiteral("deck")) {
        const auto record = model::deckFromMap(row);
        return record ? model::toMap(*record) : QVariantMap{};
    }
    if (entity == QStringLiteral("card")) {
        const auto record = model::cardFromMap(row);
        return record ? model::toMap(*record) : QVariantMap{};
    }
    if (entity == QStringLiteral("variant")) {
        const auto record = model::variantFromMap(row);
        return record ? model::toMap(*record) : QVariantMap{};
    }
    if (entity == QStringLiteral("review")) {
        const auto record = model::reviewFromMap(row);
        return record ? model::toMap(*record) : QVariantMap{};
    }
    return row;
}
struct CaptureTable {
    const char *table;
    const char *entity;
    const char *key;
    const char *filter;
    const char *fields;
};
constexpr std::array captureTables{
    CaptureTable{"decks", "deck", "id", "1", "'id',%1.id,'name',%1.name,'description',%1.description,'createdAt',%1.created_at,'parentId',%1.parent_id"},
    CaptureTable{"notes", "card", "id", "1", "'id',%1.id,'deckId',%1.deck_id,'kind',%1.kind,'front',%1.front,'back',%1.back,'tags',%1.tags,'pointCount',%1.point_count,'narrations',json(%1.narrations),'narrationFingerprints',json(%1.narration_fingerprints)"},
    // Explicit full-precision JSON numbers preserve stored REAL values for exact
    // history conflict checks and restoration of schedules and response times.
    CaptureTable{"review_variants", "variant", "id", "1", "'id',%1.id,'cardId',%1.card_id,'key',%1.variant_key,'due',%1.due,'reviewCount',%1.review_count,'stability',json(printf('%!.26g',%1.stability)),'difficulty',json(printf('%!.26g',%1.difficulty))"},
    CaptureTable{"reviews", "review", "id", "1", "'id',%1.id,'cardId',%1.card_id,'variantId',%1.variant_id,'deckName',%1.deck_name,'grade',%1.grade,'recallFraction',json(printf('%!.26g',%1.recall_fraction)),'responseSeconds',json(printf('%!.26g',%1.response_seconds)),'reviewedAt',%1.reviewed_at,'due',%1.due,'sessionId',%1.session_id,'sessionDeckName',%1.session_deck_name,'sessionStartedAt',%1.session_started_at"},
    CaptureTable{"metadata", "owner", "substr(%1.key,14)", "substr(%1.key,1,13)='review_owner:'", "'id',substr(%1.key,14),'reviewId',%1.value"},
    CaptureTable{"deck_checkpoints", "checkpoint", "id", "1", "'id',%1.id,'position',%1.position,'deckId',%1.deck_id,'createdAt',%1.created_at,'note',%1.note,'summary',%1.summary,'snapshot',json(%1.snapshot),'changes',json(%1.changes)"}
};
QString captureKey(const struct CaptureTable &table, const QString &prefix)
{
    const QString key = QString::fromLatin1(table.key);
    return key.contains(QStringLiteral("%1")) ? key.arg(prefix) : prefix + QLatin1Char('.') + key;
}
}

DatabaseWorker::HistoryScope::HistoryScope(DatabaseWorker *value, const QString &label) : worker(value)
{
    assert(worker);
    worker->assertThread();
    if (!worker->m_historyReady) {
        if (worker->m_db.isOpen()) {
            valid = false;
            worker->fail("UNDO_CAPTURE_UNAVAILABLE", "Undo history is unavailable. Restart before making more changes.");
        }
        worker = nullptr; return;
    }
    assert(!worker->m_historyCapturing);
    if (worker->m_historyCapturing) {
        valid=false; worker->fail("UNDO_CAPTURE_REENTRANT", "An undoable action is already being recorded. Wait for it to finish.");
        worker=nullptr; return;
    }
    worker->m_historyLabel = label;
    worker->m_historyCommitted = false;
    worker->m_historyRuntimeChanged = false;
    worker->m_historyBeforeSession = worker->historySession();
    if (!worker->execute("DELETE FROM temp.history_capture")
        || !worker->execute("INSERT INTO temp.history_capture_active(value) VALUES(1)")) {
        valid = false;
        worker->fail("UNDO_CAPTURE", "Could not prepare undo history. Check storage and try again.", worker->m_sqlDetail);
        worker = nullptr;
        return;
    }
    worker->m_historyCapturing = true;
}
DatabaseWorker::HistoryScope::~HistoryScope()
{
    if (worker) worker->finishHistory();
}
DatabaseWorker::HistoryScope DatabaseWorker::historyScope(const QString &label)
{
    return HistoryScope(this, label);
}
bool DatabaseWorker::initializeHistory()
{
    assertThread();
    if (!execute("PRAGMA temp_store=FILE")
        || !execute("CREATE TEMP TABLE history_frames(id TEXT PRIMARY KEY,payload BLOB NOT NULL)")
        || !execute("CREATE TEMP TABLE history_capture(entity TEXT NOT NULL,id TEXT NOT NULL,before_row TEXT,after_row TEXT,PRIMARY KEY(entity,id))")
        || !execute("CREATE TEMP TABLE history_capture_active(value INTEGER NOT NULL)")) return false;
    for (const struct CaptureTable &table : captureTables) {
        const QString entity = QString::fromLatin1(table.entity);
        const QString fields = QString::fromLatin1(table.fields);
        for (const QString &operation : {QStringLiteral("INSERT"), QStringLiteral("UPDATE"), QStringLiteral("DELETE")}) {
            const bool inserted = operation == QStringLiteral("INSERT");
            const bool deleted = operation == QStringLiteral("DELETE");
            const QString prefix = deleted ? QStringLiteral("OLD") : QStringLiteral("NEW");
            const QString before = inserted ? QStringLiteral("NULL") : QStringLiteral("json_object(%1)").arg(fields.arg(QStringLiteral("OLD")));
            const QString after = deleted ? QStringLiteral("NULL") : QStringLiteral("json_object(%1)").arg(fields.arg(QStringLiteral("NEW")));
            const QString sql = QStringLiteral("CREATE TEMP TRIGGER history_%1_%2 AFTER %3 ON main.%4 WHEN EXISTS(SELECT 1 FROM history_capture_active) AND (%5) BEGIN INSERT INTO history_capture(entity,id,before_row,after_row) VALUES('%1',%6,%7,%8) ON CONFLICT(entity,id) DO UPDATE SET after_row=excluded.after_row; END")
                .arg(entity, operation.toLower(), operation, QString::fromLatin1(table.table), (QString::fromLatin1(table.filter).contains(QStringLiteral("%1")) ? QString::fromLatin1(table.filter).arg(prefix) : QString::fromLatin1(table.filter)), captureKey(table, prefix), before, after);
            if (!execute(sql)) return false;
        }
    }
    m_historyReady = true;
    return true;
}
DatabaseWorker::HistorySession DatabaseWorker::historySession() const
{
    return {m_queue, m_sessionOrder, m_sessionGrades, m_selectedVariant, m_sessionTotal, m_sessionActive,
            m_sessionId, m_sessionDeckName, m_sessionStartedAt};
}
void DatabaseWorker::restoreHistorySession(const struct HistorySession &session)
{
    m_queue = session.queue;
    m_sessionOrder = session.order;
    m_sessionGrades = session.grades;
    m_selectedVariant = session.selected;
    m_sessionTotal = session.total;
    m_sessionActive = session.active;
    m_sessionId = session.id;
    m_sessionDeckName = session.deckName;
    m_sessionStartedAt = session.startedAt;
    m_queueCache.clear();
    m_queueDirty = true;
}
void DatabaseWorker::finishHistory()
{
    assertThread();
    assert(m_historyCapturing);
    m_historyCapturing = false;
    const QString originalError = m_sqlDetail;
    if (!execute("DELETE FROM temp.history_capture_active")) {
        m_historyReady = false;
        emit historyError("UNDO_CAPTURE", "Could not finalize undo history. Restart before making more changes.", m_sqlDetail);
        return;
    }
    if (!m_historyCommitted && !m_historyRuntimeChanged) { m_sqlDetail = originalError; return; }
    struct HistoryFrame frame;
    QVariantList changes;
    frame.id = model::uuid();
    frame.label = m_historyLabel;
    frame.beforeSession = std::move(m_historyBeforeSession);
    frame.afterSession = historySession();
    QSqlQuery query(m_db);
    if (!query.exec("SELECT entity,id,before_row,after_row FROM temp.history_capture ORDER BY entity,id")) {
        m_historyReady = false;
        emit historyError("UNDO_CAPTURE", "The action completed, but its undo record could not be read. Preserve the database and restart.", query.lastError().text());
        return;
    }
    while (query.next()) {
        const QString entity = query.value(0).toString();
        const auto parse = [&](int column) -> QVariant {
            if (query.value(column).isNull()) return absent();
            const QJsonDocument document = QJsonDocument::fromJson(query.value(column).toString().toUtf8());
            if (!document.isObject()) return QVariantMap{};
            return canonical(entity, document.object().toVariantMap());
        };
        const QVariant before = parse(2);
        const QVariant after = parse(3);
        if ((present(before) && before.toMap().isEmpty()) || (present(after) && after.toMap().isEmpty())) {
            m_historyReady = false;
            emit historyError("UNDO_CAPTURE", "The action completed, but its undo record is invalid. Preserve the database and restart.", {});
            return;
        }
        if (before == after) continue;
        changes.append(QVariantMap{{"entity", entity}, {"id", query.value(1)}, {"before", before}, {"after", after}});
    }
    if (query.lastError().isValid()) {
        m_historyReady = false;
        emit historyError("UNDO_CAPTURE", "The action completed, but its undo record could not be read.", query.lastError().text());
        return;
    }
    const auto &before = frame.beforeSession;
    const auto &after = frame.afterSession;
    const bool sessionChanged = before.queue != after.queue || before.order != after.order
        || before.selected != after.selected || before.active != after.active || before.id != after.id
        || before.total != after.total || before.grades.size() != after.grades.size();
    if (changes.isEmpty() && !sessionChanged) { m_sqlDetail = originalError; return; }
    for (const QVariant &value : changes) {
        const QVariantMap change = value.toMap();
        if (change.value("entity") != QStringLiteral("checkpoint")) continue;
        for (const QString &side : {QStringLiteral("before"), QStringLiteral("after")}) {
            for (const QVariant &entry : change.value(side).toMap().value("snapshot").toMap().value("cards").toList()) {
                const auto note = model::cardFromMap(entry.toMap());
                if (!note) { m_historyReady=false; emit historyError("UNDO_CAPTURE", "The checkpoint undo record is invalid. Restart before making more changes.", note.error()); return; }
                const auto names = model::cardMediaReferences(*note);
                if (!names) { m_historyReady=false; emit historyError("UNDO_CAPTURE", "The checkpoint media undo record is invalid. Restart before making more changes.", names.error()); return; }
                for (const QString &name : *names) frame.checkpointMedia.insert(name);
            }
        }
    }
    const QByteArray bytes = qCompress(QJsonDocument(QJsonArray::fromVariantList(changes)).toJson(QJsonDocument::Compact), 1);
    if (!execute("INSERT INTO temp.history_frames(id,payload) VALUES(?,?)", {frame.id,bytes})) {
        m_historyReady=false; emit historyError("UNDO_CAPTURE", "The action completed, but its undo record could not be stored. Restart before making more changes.", m_sqlDetail); return;
    }
    const QString id = frame.id;
    const QString label = frame.label;
    m_historyFrames.insert(id, std::move(frame));
    m_historyOrder.append(id);
    while (m_historyOrder.size() > 5) {
        const QString discarded = m_historyOrder.takeFirst(); m_historyFrames.remove(discarded);
        if (!execute("DELETE FROM temp.history_frames WHERE id=?", {discarded})) {
            m_historyReady=false; emit historyError("UNDO_CAPTURE", "Could not trim undo storage. Restart before making more changes.", m_sqlDetail);
        }
    }
    m_sqlDetail = originalError;
    emit actionRecorded(id, label);
}
void DatabaseWorker::forgetHistory(const QStringList &ids)
{
    assertThread();
    for (const QString &id : ids) {
        m_historyFrames.remove(id);
        m_historyOrder.removeAll(id);
        if (m_db.isOpen() && !execute("DELETE FROM temp.history_frames WHERE id=?", {id})) {
            m_historyReady=false; emit historyError("UNDO_CAPTURE", "Could not trim undo storage. Restart before making more changes.", m_sqlDetail);
        }
    }
}
QSet<QString> DatabaseWorker::historyMediaNames() const
{
    QSet<QString> result;
    for (const struct HistoryFrame &frame : m_historyFrames) result.unite(frame.checkpointMedia);
    return result;
}

QVariantMap DatabaseWorker::historyRow(const QString &entity, const QString &id)
{
    if (entity == QStringLiteral("deck")) { const auto r = deck(id); return r ? model::toMap(*r) : QVariantMap{}; }
    if (entity == QStringLiteral("card")) { const auto r = card(id); return r ? model::toMap(*r) : QVariantMap{}; }
    if (entity == QStringLiteral("variant")) { const auto r = variant(id); return r ? model::toMap(*r) : QVariantMap{}; }
    if (entity == QStringLiteral("review")) { const auto r = review(id); return r ? model::toMap(*r) : QVariantMap{}; }
    if (entity == QStringLiteral("owner")) {
        const QString owner = reviewOwner(id);
        return owner.isEmpty() ? QVariantMap{} : QVariantMap{{"id", id}, {"reviewId", owner}};
    }
    if (entity == QStringLiteral("checkpoint")) {
        QSqlQuery query(m_db);
        query.prepare("SELECT position,deck_id,created_at,note,summary,snapshot,changes FROM deck_checkpoints WHERE id=?");
        query.addBindValue(id);
        if (!query.exec()) { m_sqlDetail = query.lastError().text(); return {}; }
        if (!query.next()) return {};
        return {{"id", id}, {"position", query.value(0)}, {"deckId", query.value(1)}, {"createdAt", query.value(2)},
                {"note", query.value(3)}, {"summary", query.value(4)},
                {"snapshot", QJsonDocument::fromJson(query.value(5).toString().toUtf8()).object().toVariantMap()},
                {"changes", QJsonDocument::fromJson(query.value(6).toString().toUtf8()).object().toVariantMap()}};
    }
    m_sqlDetail = QStringLiteral("Unknown undo record type.");
    return {};
}

bool DatabaseWorker::applyHistoryChanges(const QVariantList &changes, bool remote, qint64 seq)
{
    m_historyValidationFailure=false;
    if (changes.isEmpty()) return true;
    QHash<QString, QVariantMap> desired;
    for (const QVariant &value : changes) {
        const QVariantMap change = value.toMap();
        const QString entity = change.value("entity").toString();
        const QString id = change.value("id").toString();
        if (remote && entity == QStringLiteral("checkpoint")) { m_historyValidationFailure=true; m_sqlDetail = QStringLiteral("Checkpoints are local records."); return false; }
        const QVariantMap current = historyRow(entity, id);
        if (!m_sqlDetail.isEmpty()) return false;
        const QVariantMap expected = change.value("before").toMap();
        if (!remote && current != expected) {
            m_historyValidationFailure=true; m_sqlDetail = QStringLiteral("The %1 record %2 changed since this action. Its current content was preserved.").arg(entity, id);
            return false;
        }
        desired.insert(rowKey(change), change.value("after").toMap());
    }
    const auto resulting = [&](const QString &entity, const QString &id) -> QVariantMap {
        const auto found = desired.constFind(entity + QLatin1Char('/') + id);
        return found == desired.cend() ? historyRow(entity, id) : *found;
    };
    // Validate the complete target hierarchy before changing any record.
    QHash<QString, QString> parents;
    QSqlQuery hierarchy(m_db);
    if (!hierarchy.exec("SELECT id,parent_id FROM decks")) { m_sqlDetail = hierarchy.lastError().text(); return false; }
    while (hierarchy.next()) parents.insert(hierarchy.value(0).toString(), hierarchy.value(1).toString());
    for (const QVariant &value : changes) {
        const QVariantMap change = value.toMap();
        if (change.value("entity") != QStringLiteral("deck")) continue;
        const QString id = change.value("id").toString();
        const QVariantMap after = change.value("after").toMap();
        if (after.isEmpty()) { if (!remote) parents.remove(id); }
        else parents.insert(id, after.value("parentId").toString());
    }
    QHash<QString, int> colors;
    for (auto it = parents.cbegin(); it != parents.cend(); ++it) {
        QString cursor = it.key();
        QStringList path;
        while (!cursor.isEmpty()) {
            if (!parents.contains(cursor)) { m_historyValidationFailure=true; m_sqlDetail = QStringLiteral("Undo would leave a deck without its parent. Its current content was preserved."); return false; }
            if (colors.value(cursor) == 2) break;
            if (colors.value(cursor) == 1) { m_historyValidationFailure=true; m_sqlDetail = QStringLiteral("Undo would create a deck hierarchy cycle. Its current content was preserved."); return false; }
            colors.insert(cursor, 1);
            path.append(cursor);
            cursor = parents.value(cursor);
        }
        for (const QString &id : path) colors.insert(id, 2);
    }
    for (const QVariant &value : changes) {
        const QVariantMap change = value.toMap();
        const QString entity = change.value("entity").toString();
        const QString id = change.value("id").toString();
        const QVariantMap after = change.value("after").toMap();
        if (!remote && entity == QStringLiteral("deck") && after.isEmpty()) {
            QSqlQuery contents(m_db);
            contents.prepare("SELECT id FROM notes WHERE deck_id=?"); contents.addBindValue(id);
            if (!contents.exec()) { m_sqlDetail = contents.lastError().text(); return false; }
            while (contents.next()) {
                const QString cardId = contents.value(0).toString();
                const QVariantMap remaining = resulting(QStringLiteral("card"), cardId);
                if (!remaining.isEmpty() && remaining.value("deckId") == id) {
                    m_historyValidationFailure=true; m_sqlDetail = QStringLiteral("This deck contains a card added after the action. Move that card before undoing the deck change."); return false;
                }
            }
        }
        if (!remote && entity == QStringLiteral("card") && after.isEmpty()) {
            for (const struct model::Variant &record : cardVariants(id)) {
                if (!resulting(QStringLiteral("variant"), record.id).isEmpty()) {
                    m_historyValidationFailure=true; m_sqlDetail = QStringLiteral("This card has a review variant changed after the action. Its current content was preserved."); return false;
                }
                if (!resulting(QStringLiteral("owner"), record.id).isEmpty()) {
                    m_historyValidationFailure=true; m_sqlDetail = QStringLiteral("This card has a newer review owner. Its current content was preserved."); return false;
                }
            }
        }
        if (after.isEmpty()) continue;
        if (entity == QStringLiteral("card")) {
            const auto record = model::cardFromMap(after);
            if (!record) { m_sqlDetail = record.error(); return false; }
            if (!parents.contains(record->deckId)) { m_historyValidationFailure=true; m_sqlDetail = QStringLiteral("The destination deck no longer exists. Restore or move the deck first."); return false; }
            if (!validateMedia(*record)) return false;
            const auto keys = model::variantKeys(*record);
            assert(keys);
            for (const QString &key : *keys) {
                const QVariantMap schedule = resulting(QStringLiteral("variant"), model::variantId(id, key));
                if (schedule.isEmpty() || schedule.value("cardId") != id || schedule.value("key") != key) {
                    m_historyValidationFailure=true; m_sqlDetail = QStringLiteral("Undo would leave a card without its independent review schedules."); return false;
                }
            }
            for (const struct model::Variant &schedule : cardVariants(id)) {
                const QVariantMap remaining = resulting(QStringLiteral("variant"), schedule.id);
                if (!remote && !remaining.isEmpty() && !keys->contains(remaining.value("key").toString())) {
                    m_historyValidationFailure=true; m_sqlDetail = QStringLiteral("A review variant changed after this card action. Its current schedule was preserved."); return false;
                }
            }
        }
        if (entity == QStringLiteral("variant")) {
            const auto source = model::cardFromMap(resulting(QStringLiteral("card"), after.value("cardId").toString()));
            if (!source || !model::variantKeys(*source)->contains(after.value("key").toString())) {
                m_historyValidationFailure=true; m_sqlDetail = QStringLiteral("The card or variant definition changed. Its current schedule was preserved."); return false;
            }
        }
        if (entity == QStringLiteral("owner")) {
            const QVariantMap schedule = resulting(QStringLiteral("variant"), id);
            const QVariantMap record = resulting(QStringLiteral("review"), after.value("reviewId").toString());
            if (schedule.isEmpty() || record.isEmpty() || record.value("variantId") != id
                || record.value("cardId") != schedule.value("cardId")) {
                m_historyValidationFailure=true; m_sqlDetail = QStringLiteral("Undo would attach a schedule to an unavailable review."); return false;
            }
        }
    }
    if (!m_sqlDetail.isEmpty()) return false;
    // Deck parents are validated as a graph; insertion order cannot expose an
    // intermediate hierarchy to readers because the surrounding transaction owns it.
    for (const QVariant &value : changes) {
        const QVariantMap change = value.toMap();
        const QString entity = change.value("entity").toString();
        const QVariantMap after = change.value("after").toMap();
        if (entity != QStringLiteral("deck") || after.isEmpty()) continue;
        const auto record = model::deckFromMap(after);
        if (!record) { m_sqlDetail = record.error(); return false; }
        if (!upsertDeck(*record) || !execute("DELETE FROM tombstones WHERE entity_type='deck' AND entity_id=?", {record->id})) return false;
    }
    for (const QString &entity : {QStringLiteral("card"), QStringLiteral("variant"), QStringLiteral("review"), QStringLiteral("owner"), QStringLiteral("checkpoint")}) {
        for (const QVariant &value : changes) {
            const QVariantMap change = value.toMap();
            const QString id = change.value("id").toString();
            const QVariantMap after = change.value("after").toMap();
            if (change.value("entity") != entity || after.isEmpty()) continue;
            if (entity == QStringLiteral("card")) {
                const auto record = model::cardFromMap(after);
                assert(record);
                if (!upsertCard(*record) || !execute("DELETE FROM tombstones WHERE entity_type='card' AND entity_id=?", {id})) return false;
                if (remote) {
                    const auto keys=model::variantKeys(*record);
                    assert(keys);
                    for (const struct model::Variant &schedule:cardVariants(id)) {
                        if (keys->contains(schedule.key)) continue;
                        if (!tombstone("variant",schedule.id,seq)
                            || !execute("DELETE FROM metadata WHERE key=?",{QStringLiteral("review_owner:")+schedule.id})
                            || !execute("DELETE FROM review_variants WHERE id=?",{schedule.id})) return false;
                    }
                }
            } else if (entity == QStringLiteral("variant")) {
                const auto record = model::variantFromMap(after);
                if (!record) { m_sqlDetail = record.error(); return false; }
                QString owner;
                const auto explicitOwner=desired.constFind(QStringLiteral("owner/")+id);
                if (explicitOwner!=desired.cend()) owner=explicitOwner->value("reviewId").toString();
                else {
                    for (const QVariant &candidate:changes) {
                        const QVariantMap entry=candidate.toMap();
                        const QVariantMap result=entry.value("after").toMap();
                        if (entry.value("entity")==QStringLiteral("review") && result.value("variantId")==id) {
                            owner=result.value("id").toString(); break;
                        }
                    }
                    if (!remote && owner.isEmpty()) owner=reviewOwner(id);
                }
                if (!m_sqlDetail.isEmpty() || !upsertVariant(*record, owner)) return false;
                if (remote && owner.isEmpty() && !execute("DELETE FROM metadata WHERE key=?",{QStringLiteral("review_owner:")+id})) return false;
            } else if (entity == QStringLiteral("review")) {
                const auto record = model::reviewFromMap(after);
                if (!record) { m_sqlDetail = record.error(); return false; }
                if (!execute("DELETE FROM reviews WHERE id=?", {id}) || !insertReview(*record)) return false;
            } else if (entity == QStringLiteral("owner")) {
                if (!execute("INSERT INTO metadata(key,value) VALUES(?,?) ON CONFLICT(key) DO UPDATE SET value=excluded.value",
                    {QStringLiteral("review_owner:") + id, after.value("reviewId")})) return false;
            } else if (entity == QStringLiteral("checkpoint")) {
                QVariantMap validated = after; validated.remove(QStringLiteral("position"));
                const auto record = model::checkpointFromMap(validated);
                if (!record) { m_sqlDetail = record.error(); return false; }
                if (!execute("INSERT INTO deck_checkpoints(position,id,deck_id,created_at,note,summary,snapshot,changes) VALUES(?,?,?,?,?,?,?,?) ON CONFLICT(id) DO UPDATE SET position=excluded.position,deck_id=excluded.deck_id,created_at=excluded.created_at,note=excluded.note,summary=excluded.summary,snapshot=excluded.snapshot,changes=excluded.changes",
                    {after.value("position"), id, after.value("deckId"), after.value("createdAt"), after.value("note"), after.value("summary"), encoded(after.value("snapshot").toMap()), encoded(after.value("changes").toMap())})) return false;
            }
        }
    }
    for (const QString &entity : {QStringLiteral("owner"), QStringLiteral("variant"), QStringLiteral("card"), QStringLiteral("deck"), QStringLiteral("review"), QStringLiteral("checkpoint")}) {
        for (const QVariant &value : changes) {
            const QVariantMap change = value.toMap();
            const QString id = change.value("id").toString();
            if (change.value("entity") != entity || present(change.value("after"))) continue;
            if (entity == QStringLiteral("owner")) {
                if (!execute("DELETE FROM metadata WHERE key=?", {QStringLiteral("review_owner:") + id})) return false;
            } else if (entity == QStringLiteral("variant")) {
                if (!tombstone(entity, id, seq) || !execute("DELETE FROM review_variants WHERE id=?", {id})) return false;
            } else if (entity == QStringLiteral("card")) {
                if (remote) {
                    if (seq < entitySequence("restore-card",id)) continue;
                    if (!eraseCard(id,seq)) return false;
                } else if (!tombstone(entity, id, seq) || !execute("DELETE FROM notes WHERE id=?", {id})) return false;
            } else if (entity == QStringLiteral("deck")) {
                if (remote) {
                    bool applied=true;
                    if (!applyDeckDelete(id,seq,applied)) return false;
                    if (!applied) { m_historyValidationFailure=true; m_sqlDetail=QStringLiteral("The deck deletion is waiting for a pending local restore."); return false; }
                } else if (!tombstone(entity, id, seq) || !execute("DELETE FROM decks WHERE id=?", {id})) return false;
            } else if (entity == QStringLiteral("review")) {
                if (!execute("DELETE FROM reviews WHERE id=?", {id})) return false;
            } else if (!execute("DELETE FROM deck_checkpoints WHERE id=?", {id})) return false;
        }
    }
    return true;
}

bool DatabaseWorker::enqueueHistory(const QVariantList &changes)
{
    // Each card and its independent schedules form one dependency-complete unit.
    // Root folders precede children; deleted folders follow their removed contents.
    QHash<QString, QVariantMap> indexed;
    QHash<QString, QString> parents;
    QSqlQuery currentDecks(m_db);
    if (!currentDecks.exec("SELECT id,parent_id FROM decks")) { m_sqlDetail = currentDecks.lastError().text(); return false; }
    while (currentDecks.next()) parents.insert(currentDecks.value(0).toString(), currentDecks.value(1).toString());
    for (const QVariant &value : changes) {
        const QVariantMap change = value.toMap();
        if (change.value("entity") == QStringLiteral("checkpoint")) continue;
        indexed.insert(rowKey(change), change);
        if (change.value("entity") == QStringLiteral("deck")) {
            const QVariantMap record = change.value("after").toMap();
            if (!record.isEmpty()) parents.insert(change.value("id").toString(), record.value("parentId").toString());
        }
    }
    const QList<QVariantMap> actualChanges=indexed.values();
    for (const QVariantMap &change:actualChanges) {
        if (change.value("entity")!=QStringLiteral("card") || !present(change.value("after"))) continue;
        for (const struct model::Variant &schedule:cardVariants(change.value("id").toString())) {
            const QString variantKey=QStringLiteral("variant/")+schedule.id;
            if (indexed.contains(variantKey)) continue;
            indexed.insert(variantKey,{{"entity","variant"},{"id",schedule.id},{"before",absent()},
                {"after",model::toMap(schedule)},{"contextOnly",true}});
            const QString owner=reviewOwner(schedule.id);
            const QString ownerKey=QStringLiteral("owner/")+schedule.id;
            if (!owner.isEmpty() && !indexed.contains(ownerKey))
                indexed.insert(ownerKey,{{"entity","owner"},{"id",schedule.id},{"before",absent()},
                    {"after",QVariantMap{{"id",schedule.id},{"reviewId",owner}}},{"contextOnly",true}});
        }
    }
    if (!m_sqlDetail.isEmpty()) return false;
    const auto depth = [&](const QVariantMap &change, bool before) {
        const QVariantMap record = change.value(before ? QStringLiteral("before") : QStringLiteral("after")).toMap();
        QString cursor = record.value("parentId").toString();
        QSet<QString> visited;
        int result = 0;
        while (!cursor.isEmpty() && !visited.contains(cursor)) {
            visited.insert(cursor); ++result;
            const auto changed = indexed.constFind(QStringLiteral("deck/") + cursor);
            if (before && changed != indexed.cend() && present(changed->value("before")))
                cursor = changed->value("before").toMap().value("parentId").toString();
            else cursor = parents.value(cursor);
        }
        return result;
    };
    QList<QVariantMap> deckWrites, deckDeletes;
    for (const QVariantMap &change : indexed) {
        if (change.value("entity") != QStringLiteral("deck")) continue;
        (present(change.value("after")) ? deckWrites : deckDeletes).append(change);
    }
    std::sort(deckWrites.begin(), deckWrites.end(), [&](const QVariantMap &a, const QVariantMap &b) {
        const int left = depth(a, false), right = depth(b, false);
        return left == right ? a.value("id").toString() < b.value("id").toString() : left < right;
    });
    std::sort(deckDeletes.begin(), deckDeletes.end(), [&](const QVariantMap &a, const QVariantMap &b) {
        const int left = depth(a, true), right = depth(b, true);
        return left == right ? a.value("id").toString() < b.value("id").toString() : left > right;
    });
    QList<QVariantList> groups;
    QSet<QString> consumed;
    for (const QVariantMap &change : deckWrites) { groups.append(QVariantList{change}); consumed.insert(rowKey(change)); }
    QMap<QString, QVariantList> cardGroups;
    const auto cardOf = [&](const QVariantMap &change) {
        const QString entity = change.value("entity").toString();
        const QVariantMap record = change.value(present(change.value("after")) ? QStringLiteral("after") : QStringLiteral("before")).toMap();
        if (entity == QStringLiteral("card")) return change.value("id").toString();
        if (entity == QStringLiteral("variant") || entity == QStringLiteral("review")) return record.value("cardId").toString();
        if (entity == QStringLiteral("owner")) {
            const auto schedule = indexed.constFind(QStringLiteral("variant/") + change.value("id").toString());
            if (schedule != indexed.cend()) {
                const QVariantMap variant = schedule->value(present(schedule->value("after")) ? QStringLiteral("after") : QStringLiteral("before")).toMap();
                return variant.value("cardId").toString();
            }
            const auto variant = this->variant(change.value("id").toString());
            if (variant) return variant->cardId;
            for (const QString &side : {QStringLiteral("after"), QStringLiteral("before")}) {
                const QString reviewId = change.value(side).toMap().value("reviewId").toString();
                const auto record = indexed.constFind(QStringLiteral("review/") + reviewId);
                if (record != indexed.cend()) {
                    const QVariantMap review = record->value(present(record->value("after")) ? QStringLiteral("after") : QStringLiteral("before")).toMap();
                    return review.value("cardId").toString();
                }
                const auto review = this->review(reviewId);
                if (review) return review->cardId;
            }
        }
        return QString();
    };
    for (const QVariantMap &change : indexed) {
        const QString entity = change.value("entity").toString();
        if (entity != QStringLiteral("card") && entity != QStringLiteral("variant") && entity != QStringLiteral("owner")) continue;
        const QString cardId = cardOf(change);
        if (cardId.isEmpty()) { m_sqlDetail = QStringLiteral("Could not identify the card required by an undo change."); return false; }
        cardGroups[cardId].append(change); consumed.insert(rowKey(change));
    }
    for (auto group = cardGroups.begin(); group != cardGroups.end(); ++group) {
        QSet<QString> reviewIds;
        for (const QVariant &value : *group) {
            const QVariantMap change = value.toMap();
            if (change.value("entity") != QStringLiteral("owner")) continue;
            for (const QString &side : {QStringLiteral("before"), QStringLiteral("after")})
                reviewIds.insert(change.value(side).toMap().value("reviewId").toString());
        }
        for (const QString &id : reviewIds) {
            const auto review = indexed.constFind(QStringLiteral("review/") + id);
            if (review == indexed.cend()) continue;
            group->append(*review); consumed.insert(review.key());
        }
        groups.append(*group);
    }
    QStringList remaining = indexed.keys(); std::sort(remaining.begin(), remaining.end());
    for (const QString &key : remaining)
        if (!consumed.contains(key) && indexed.value(key).value("entity") != QStringLiteral("deck")) groups.append(QVariantList{indexed.value(key)});
    for (const QVariantMap &change : deckDeletes) groups.append(QVariantList{change});
    const QString mediaDirectory = QDir(QFileInfo(m_path).absolutePath()).filePath(QStringLiteral("media"));
    QVariantList batch;
    QSet<QString> batchMedia;
    qint64 batchMediaBytes = 0;
    qint64 batchBytes = 512;
    const auto fits = [](qint64 bytes, const QSet<QString> &media, qint64 mediaBytes) {
        return bytes <= model::maxSyncEventBytes && media.size() <= model::maxSyncImages && mediaBytes <= model::maxMediaBytes;
    };
    for (const QVariantList &group : groups) {
        QSet<QString> names;
        for (const QVariant &value : group) {
            const QVariantMap change = value.toMap();
            if (change.value("entity") != QStringLiteral("card")) continue;
            for (const QString &side : {QStringLiteral("after")}) {
                if (!present(change.value(side))) continue;
                const auto card = model::cardFromMap(change.value(side).toMap());
                assert(card);
                const auto references = model::cardMediaReferences(*card);
                assert(references);
                for (const QString &name : *references) names.insert(name);
            }
        }
        const auto media = model::readBackupMedia(mediaDirectory, names.values());
        if (!media) { m_sqlDetail = media.error(); return false; }
        QHash<QString, qsizetype> sizes;
        for (const struct model::MediaAttachment &attachment : *media) sizes.insert(attachment.name, attachment.bytes.size());
        const auto additionalBytes = [&](const QSet<QString> &existing) {
            qint64 result = 0; for (auto it = sizes.cbegin(); it != sizes.cend(); ++it) if (!existing.contains(it.key())) result += it.value(); return result;
        };
        const qint64 groupBytes = QJsonDocument(QJsonArray::fromVariantList(group)).toJson(QJsonDocument::Compact).size() - 2;
        QSet<QString> candidateMedia = batchMedia; candidateMedia.unite(names);
        if (!batch.isEmpty() && !fits(batchBytes + groupBytes + 1, candidateMedia, batchMediaBytes + additionalBytes(batchMedia))) {
            if (!enqueue("history.apply", {{"changes", batch}})) return false;
            batch.clear(); batchMedia.clear(); batchMediaBytes = 0; batchBytes = 512;
        }
        batchBytes += groupBytes + (batch.isEmpty() ? 0 : 1);
        batch += group; batchMediaBytes += additionalBytes(batchMedia); batchMedia.unite(names);
        if (!fits(batchBytes, batchMedia, batchMediaBytes)) {
            m_sqlDetail = QStringLiteral("A card and its dependent review changes exceed the undo sync limit. Its current content was preserved."); return false;
        }
    }
    return batch.isEmpty() || enqueue("history.apply", {{"changes", batch}});
}

void DatabaseWorker::applyHistory(const QString &id, bool redo)
{
    if (!ready()) { emit historyApplied(id, redo, false); return; }
    const auto found = m_historyFrames.constFind(id);
    if (found == m_historyFrames.cend() || found->applied == redo) {
        fail("UNDO_UNAVAILABLE", "This action is no longer available in undo history."); emit historyApplied(id, redo, false); return;
    }
    const struct HistoryFrame frame = *found;
    QSqlQuery stored(m_db);
    stored.prepare("SELECT payload FROM temp.history_frames WHERE id=?"); stored.addBindValue(id);
    if (!stored.exec() || !stored.next()) {
        fail("UNDO_STORAGE", "Could not read this action from undo storage. Current records were preserved.", stored.lastError().text());
        emit historyApplied(id, redo, false); return;
    }
    const QByteArray bytes = qUncompress(stored.value(0).toByteArray());
    const QJsonDocument document = QJsonDocument::fromJson(bytes);
    if (!document.isArray()) {
        fail("UNDO_STORAGE", "This undo record is invalid. Current records were preserved. Restart to clear undo history.");
        emit historyApplied(id, redo, false); return;
    }
    QVariantList changes = document.array().toVariantList();
    if (!redo) {
        for (QVariant &value : changes) {
            QVariantMap change = value.toMap();
            const QVariant before = change.value("before");
            change.insert("before", change.value("after")); change.insert("after", before); value = change;
        }
    }
    if (!mutate([&] { return applyHistoryChanges(changes, false) && enqueueHistory(changes); })) {
        fail("UNDO_CONFLICT", QStringLiteral("Could not %1 %2. Current records were preserved. Sync or resolve the changed records before trying again.").arg(redo ? QStringLiteral("redo") : QStringLiteral("undo"), frame.label.toLower()), m_sqlDetail);
        emit historyApplied(id, redo, false); return;
    }
    m_historyFrames[id].applied = redo;
    restoreHistorySession(redo ? frame.afterSession : frame.beforeSession);
    const bool refreshed = publishSnapshot() && publishQueue(true);
    emit historyApplied(id, redo, true);
    if (refreshed) done(QStringLiteral("%1: %2").arg(redo ? QStringLiteral("Redone") : QStringLiteral("Undone"), frame.label));
}
void DatabaseWorker::recordReviewUiAction(const QString &label)
{
    if (!ready()) return;
    if (!m_historyReady) { fail("UNDO_CAPTURE_UNAVAILABLE", "Undo history is unavailable. Restart before changing the review state."); return; }
    if (label.isEmpty() || label.size() > 128 || label.contains(QChar::Null)) {
        fail("UNDO_ACTION_INVALID", "Choose a valid review action."); return;
    }
    struct HistoryFrame frame;
    frame.id = model::uuid(); frame.label = label;
    frame.beforeSession = historySession(); frame.afterSession = frame.beforeSession;
    const QString id = frame.id;
    if (!execute("INSERT INTO temp.history_frames(id,payload) VALUES(?,?)", {id,qCompress(QByteArrayLiteral("[]"),1)})) {
        fail("UNDO_STORAGE", "Could not store the review action. Check storage and try again.", m_sqlDetail); return;
    }
    m_historyFrames.insert(id, std::move(frame)); m_historyOrder.append(id);
    while (m_historyOrder.size() > 5) {
        const QString discarded = m_historyOrder.takeFirst(); m_historyFrames.remove(discarded);
        if (!execute("DELETE FROM temp.history_frames WHERE id=?", {discarded})) {
            m_historyReady=false; emit historyError("UNDO_CAPTURE", "Could not trim undo storage. Restart before making more changes.", m_sqlDetail);
        }
    }
    done(label);
    emit actionRecorded(id, label);
}

bool DatabaseWorker::pendingHistoryEntity(const QString &type, const QString &id, bool restoreOnly)
{
    QSqlQuery query(m_db);
    query.prepare("SELECT 1 FROM local_events AS event,json_each(event.event,'$.payload.changes') AS change "
                  "WHERE event.echoed=0 AND json_extract(event.event,'$.type')='history.apply' AND COALESCE(json_extract(change.value,'$.contextOnly'),0)=0 "
                  "AND (json_extract(change.value,'$.entity')=? OR (?='variant' AND json_extract(change.value,'$.entity')='owner')) AND json_extract(change.value,'$.id')=?"
                  + QString(restoreOnly ? " AND json_type(change.value,'$.after')='object'" : "") + " LIMIT 1");
    query.addBindValue(type); query.addBindValue(type); query.addBindValue(id);
    if (!query.exec()) { m_sqlDetail = query.lastError().text(); return false; }
    return query.next();
}
bool DatabaseWorker::historyEventEffects(const QVariantList &changes, qint64 seq, bool executeEffects, bool &applied)
{
    applied=true;
    if (!executeEffects) {
        for (const QVariant &value:changes) {
            const QVariantMap change=value.toMap();
            if (change.value("contextOnly").toBool()) continue;
            const QString entity=change.value("entity").toString();
            const QString id=change.value("id").toString();
            if (!recordSequence(entity==QStringLiteral("owner")?QStringLiteral("variant"):entity,id,seq)) return false;
            if (present(change.value("after")) && entity==QStringLiteral("card") && !recordSequence("restore-card",id,seq)) return false;
            if (present(change.value("after")) && entity==QStringLiteral("deck") && !recordSequence("restore-container",id,seq)) return false;
        }
        return true;
    }
    QVariantList effects;
    QHash<QString,QVariantMap> incomingCards,incomingVariants;
    QSet<QString> createdContextVariants;
    bool pending=false;
    for (const QVariant &value:changes) {
        const QVariantMap change=value.toMap();
        if (change.value("contextOnly").toBool()) continue;
        const QString entity=change.value("entity").toString();
        const QString id=change.value("id").toString();
        const QString clockEntity=entity==QStringLiteral("owner")?QStringLiteral("variant"):entity;
        if (pendingEntity(clockEntity,id)) { pending=true; continue; }
        if (!m_sqlDetail.isEmpty()) return false;
        const bool deletion=!present(change.value("after"));
        const bool structuralDelete=deletion && (entity==QStringLiteral("card") || entity==QStringLiteral("deck"));
        // Deletion uses the same existence barriers as the ordinary delete
        // events. A later review does not resurrect the reviewed card.
        if (!structuralDelete && seq<=entitySequence(clockEntity,id)) continue;
        if (structuralDelete && entity==QStringLiteral("card") && seq<entitySequence("restore-card",id)) continue;
        if (!m_sqlDetail.isEmpty()) return false;
        effects.append(change);
        if (entity==QStringLiteral("card")) incomingCards.insert(id,change.value("after").toMap());
        if (entity==QStringLiteral("variant")) incomingVariants.insert(id,change.value("after").toMap());
    }
    const auto usableVariant=[&](const QVariantMap &schedule) {
        const QString cardId=schedule.value("cardId").toString();
        const auto found=incomingCards.constFind(cardId);
        const QVariantMap source=found==incomingCards.cend()?historyRow(QStringLiteral("card"),cardId):*found;
        const auto card=model::cardFromMap(source);
        return card && model::variantKeys(*card)->contains(schedule.value("key").toString());
    };
    // Direction context restores only missing schedules. Existing reviews and
    // explicitly reset schedule ownership belong to their own server order.
    for (const QVariant &value:changes) {
        const QVariantMap change=value.toMap();
        if (!change.value("contextOnly").toBool() || change.value("entity")!=QStringLiteral("variant")) continue;
        const QString id=change.value("id").toString();
        const QVariantMap schedule=change.value("after").toMap();
        if (!historyRow("variant",id).isEmpty() || !usableVariant(schedule)) continue;
        if (!m_sqlDetail.isEmpty()) return false;
        const QString cardId=schedule.value("cardId").toString();
        bool cardWaiting=false;
        for (const QVariant &candidate:changes) {
            const QVariantMap entry=candidate.toMap();
            if (entry.value("entity")==QStringLiteral("card") && entry.value("id")==cardId
                && pendingEntity("card",cardId)) { cardWaiting=true; break; }
        }
        if (cardWaiting) { pending=true; continue; }
        effects.append(change); incomingVariants.insert(id,schedule); createdContextVariants.insert(id);
    }
    for (const QVariant &value:changes) {
        const QVariantMap change=value.toMap();
        if (!change.value("contextOnly").toBool() || change.value("entity")!=QStringLiteral("owner")) continue;
        const QString id=change.value("id").toString();
        const QVariantMap after=change.value("after").toMap();
        if (!createdContextVariants.contains(id) || !review(after.value("reviewId").toString())) continue;
        effects.append(change);
    }
    QHash<QString,QVariantMap> incomingReviews;
    for (const QVariant &value:effects) {
        const QVariantMap change=value.toMap();
        if (change.value("entity")==QStringLiteral("review")) incomingReviews.insert(change.value("id").toString(),change.value("after").toMap());
    }
    QVariantList usable;
    for (const QVariant &value:effects) {
        const QVariantMap change=value.toMap();
        const QString entity=change.value("entity").toString();
        const QVariantMap after=change.value("after").toMap();
        if (entity==QStringLiteral("variant") && !after.isEmpty() && !usableVariant(after)) continue;
        if (entity==QStringLiteral("owner") && !after.isEmpty()) {
            const QString id=change.value("id").toString();
            const auto found=incomingVariants.constFind(id);
            const QVariantMap schedule=found==incomingVariants.cend()?historyRow("variant",id):*found;
            if (schedule.isEmpty() || !usableVariant(schedule)) continue;
            const QString reviewId=after.value("reviewId").toString();
            const auto record=incomingReviews.constFind(reviewId);
            if (record==incomingReviews.cend() ? !review(reviewId).has_value() : record->isEmpty()) continue;
        }
        usable.append(change);
    }
    if (!m_sqlDetail.isEmpty()) return false;
    if (!execute("SAVEPOINT apply_history_event")) return false;
    if (!applyHistoryChanges(usable,true,seq)) {
        const QString detail=m_sqlDetail;
        const bool validationFailure=m_historyValidationFailure;
        if (!execute("ROLLBACK TO apply_history_event") || !execute("RELEASE apply_history_event")) return false;
        if (!validationFailure) {m_sqlDetail=detail;return false;}
        if (detail.contains(QStringLiteral("destination deck no longer exists"))
            || detail.contains(QStringLiteral("without its parent"))
            || detail.contains(QStringLiteral("unavailable review"))
            || detail.contains(QStringLiteral("waiting for a pending local restore"))) {
            applied=false; m_sqlDetail.clear(); return true;
        }
        m_syncHistoryConflicts.append(changes.first().toMap().value("id").toString());
        m_sqlDetail.clear(); return true;
    }
    if (!execute("RELEASE apply_history_event")) return false;
    for (const QVariant &value:usable) {
        const QVariantMap change=value.toMap();
        const QString entity=change.value("entity").toString();
        const QString id=change.value("id").toString();
        if (!recordSequence(entity==QStringLiteral("owner")?QStringLiteral("variant"):entity,id,seq)) return false;
        if (present(change.value("after")) && entity==QStringLiteral("card") && !recordSequence("restore-card",id,seq)) return false;
        if (present(change.value("after")) && entity==QStringLiteral("deck") && !recordSequence("restore-container",id,seq)) return false;
    }
    applied=!pending;
    return true;
}
