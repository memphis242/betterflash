#include "syncprotocol.h"
#include "cardmodel.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QVariantList>
#include <cmath>

namespace model = betterflash::model;
namespace betterflash::protocol {
namespace {
QVariantMap object(const QVariantMap &map,const QString &key) {return map.value(key).toMap();}
QString json(const QVariantMap &map) {return QString::fromUtf8(QJsonDocument(QJsonObject::fromVariantMap(map)).toJson(QJsonDocument::Compact));}
bool integer(const QVariant &value,qint64 minimum,qint64 maximum)
{
    switch (value.metaType().id()) {
    case QMetaType::Double: case QMetaType::Int: case QMetaType::UInt:
    case QMetaType::LongLong: case QMetaType::ULongLong: break;
    default: return false;
    }
    const double n=value.toDouble();
    return std::isfinite(n)&&n>=static_cast<double>(minimum)&&n<=static_cast<double>(maximum)&&std::floor(n)==n;
}
bool historyNull(const QVariant &value)
{
    return !value.isValid() || value.metaType().id() == QMetaType::Nullptr;
}
bool historyRecord(const QString &entity, const QString &id, const QVariant &value, QString &detail)
{
    if (historyNull(value)) return true;
    if (value.metaType().id() != QMetaType::QVariantMap) {
        detail = "History states must be objects or explicit null values."; return false;
    }
    const QVariantMap record = value.toMap();
    if (record.value("id").metaType().id() != QMetaType::QString || record.value("id").toString() != id) {
        detail = "History state identifiers must match their change identifier."; return false;
    }
    const QStringList entities{QStringLiteral("deck"), QStringLiteral("card"), QStringLiteral("variant"),
                               QStringLiteral("review"), QStringLiteral("owner")};
    switch (entities.indexOf(entity)) {
    case 0: {
        const auto valid = model::deckFromMap(record);
        if (!valid) { detail = valid.error(); return false; }
        return true;
    }
    case 1: {
        if (record.value("deckId").metaType().id() != QMetaType::QString) {
            detail = "History card deck identifiers must be UUID strings."; return false;
        }
        const auto valid = model::cardFromMap(record);
        if (!valid) { detail = valid.error(); return false; }
        return true;
    }
    case 2: {
        if (record.value("cardId").metaType().id() != QMetaType::QString) {
            detail = "History variant card identifiers must be UUID strings."; return false;
        }
        const auto valid = model::variantFromMap(record);
        if (!valid) { detail = valid.error(); return false; }
        return true;
    }
    case 3: {
        if (record.value("cardId").metaType().id() != QMetaType::QString
            || record.value("variantId").metaType().id() != QMetaType::QString) {
            detail = "History review dependencies must be UUID strings."; return false;
        }
        const auto valid = model::reviewFromMap(record);
        if (!valid) { detail = valid.error(); return false; }
        return true;
    }
    case 4:
        if (record.size() != 2 || record.value("reviewId").metaType().id() != QMetaType::QString
            || !model::validUuid(record.value("reviewId").toString())) {
            detail = "History schedule ownership must contain its variant and review identifiers."; return false;
        }
        return true;
    default:
        detail = "History changes contain an unsupported entity."; return false;
    }
}
bool historyChanges(const QVariantMap &payload, QString &detail)
{
    if (payload.value("changes").metaType().id() != QMetaType::QVariantList) {
        detail = "History events must contain a changes array."; return false;
    }
    const QVariantList changes = payload.value("changes").toList();
    if (changes.isEmpty() || changes.size() > model::maxCollectionRecords) {
        detail = "History events must contain between 1 and 100000 changes."; return false;
    }
    const QSet<QString> entities{QStringLiteral("deck"), QStringLiteral("card"), QStringLiteral("variant"),
                                 QStringLiteral("review"), QStringLiteral("owner")};
    QSet<QString> seen, contextVariants, contextOwners;
    for (const QVariant &value : changes) {
        if (value.metaType().id() != QMetaType::QVariantMap) {
            detail = "Each history change must be an object."; return false;
        }
        const QVariantMap change = value.toMap();
        const QString entity = change.value("entity").toString();
        const QString id = change.value("id").toString();
        if (change.value("entity").metaType().id() != QMetaType::QString || !entities.contains(entity)
            || change.value("id").metaType().id() != QMetaType::QString || !model::validUuid(id)
            || !change.contains("before") || !change.contains("after")) {
            detail = "History changes require an entity, UUID, and explicit before and after states."; return false;
        }
        const QString identity = entity + QLatin1Char('/') + id;
        if (seen.contains(identity)) {
            detail = "History changes must identify each entity only once."; return false;
        }
        seen.insert(identity);
        const QVariant before = change.value("before");
        const QVariant after = change.value("after");
        if (change.contains("contextOnly")) {
            if (change.value("contextOnly").metaType().id() != QMetaType::Bool
                || !change.value("contextOnly").toBool()
                || (entity != QStringLiteral("variant") && entity != QStringLiteral("owner"))
                || !historyNull(before) || historyNull(after)) {
                detail = "History context must be an explicit creation context for a variant or its owner."; return false;
            }
            if (entity == QStringLiteral("variant")) contextVariants.insert(id);
            else contextOwners.insert(id);
        }
        if ((historyNull(before) && historyNull(after)) || before == after) {
            detail = "Each history change must change at least one existing state."; return false;
        }
        if (!historyRecord(entity, id, before, detail) || !historyRecord(entity, id, after, detail)) return false;
    }
    for (const QString &id : contextOwners) {
        if (!contextVariants.contains(id)) {
            detail = "History owner context requires matching variant context in the same event."; return false;
        }
    }
    return true;
}
bool validateEventImpl(const QVariantMap &event, QString &detail)
{
    if (!model::validUuid(event.value("id").toString())||!model::validUuid(event.value("deviceId").toString())
        ||event.value("payload").metaType().id()!=QMetaType::QVariantMap||!model::utcInstant(event.value("createdAt").toString())
        ||json(event).toUtf8().size()>model::maxSyncEventBytes) {detail="Sync event envelope is invalid or exceeds its size limit.";return false;}
    const QString type=event.value("type").toString();
    const QVariantMap payload=object(event,"payload");
    if (payload.contains("restore")
        && ((type != QStringLiteral("deck.upsert") && type != QStringLiteral("card.upsert"))
            || payload.value("restore").metaType().id() != QMetaType::Bool)) {
        detail="Restore intent must be a boolean on a deck or card upsert."; return false;
    }
    if (payload.contains("freshVariantIds")
        && (type != QStringLiteral("card.upsert") || !payload.value("restore").toBool()
            || payload.value("freshVariantIds").metaType().id() != QMetaType::QVariantList)) {
        detail="Fresh variant identifiers belong only to an explicit card restore."; return false;
    }
    if (payload.contains("restoreDependency")
        && (type != QStringLiteral("deck.upsert") || !payload.value("restore").toBool()
            || payload.value("restoreDependency").metaType().id() != QMetaType::Bool)) {
        detail="A restore dependency must be a boolean on an explicit deck restore."; return false;
    }
    if (payload.contains("historyOnly")
        && ((type != QStringLiteral("review.add") && type != QStringLiteral("review.correct"))
            || payload.value("historyOnly").metaType().id() != QMetaType::Bool)) {
        detail="historyOnly must be a boolean on a review event."; return false;
    }
    if (type == QStringLiteral("history.apply")) return historyChanges(payload, detail);
    if (type==QStringLiteral("deck.upsert")) {
        const auto record=model::deckFromMap(object(payload,"deck"));
        if (!record) {detail=record.error();return false;}return true;
    }
    if (type==QStringLiteral("deck.delete")||type==QStringLiteral("card.delete")) {
        if (!model::validUuid(payload.value("id").toString())) {detail="Delete event has an invalid identifier.";return false;}return true;
    }
    if (type==QStringLiteral("card.upsert")) {
        const auto record=model::cardFromMap(object(payload,"card"));
        if (!record) {detail=record.error();return false;}
        if (payload.value("variants").metaType().id()!=QMetaType::QVariantList) {detail="Card event must contain variants.";return false;}
        QSet<QString> keys,ids;
        const QStringList expected=*model::variantKeys(*record);
        const QVariantList variants=payload.value("variants").toList();
        if (variants.size()!=expected.size()) {detail="Card event does not contain exactly its review variants.";return false;}
        for (const QVariant &value : variants) {
            const auto schedule=model::variantFromMap(value.toMap());
            if (!schedule) {detail=schedule.error();return false;}
            if (schedule->cardId!=record->id||!expected.contains(schedule->key)||keys.contains(schedule->key)||ids.contains(schedule->id)) {detail="Variant source or key is duplicated or invalid.";return false;}
            keys.insert(schedule->key);ids.insert(schedule->id);
        }
        if (payload.value("restore").toBool()) {
            if (payload.value("freshVariantIds").metaType().id()!=QMetaType::QVariantList) {
                detail="A card restore must identify its newly created review variants."; return false;
            }
            const QVariantList fresh = payload.value("freshVariantIds").toList();
            if (fresh.size()>variants.size()) {detail="A card restore has too many fresh variant identifiers.";return false;}
            QSet<QString> seen;
            for (const QVariant &value : fresh) {
                const QString id=value.toString();
                if (value.metaType().id()!=QMetaType::QString||!ids.contains(id)||seen.contains(id)) {
                    detail="Fresh variant identifiers must be unique identifiers from this card's variants.";return false;
                }
                seen.insert(id);
            }
        }
        return true;
    }
    if (type==QStringLiteral("variant.upsert")) {
        const auto schedule=model::variantFromMap(object(payload,"variant"));
        if (!schedule) {detail=schedule.error();return false;}
        return true;
    }
    if (type==QStringLiteral("review.add")||type==QStringLiteral("review.correct")) {
        const bool correction=type==QStringLiteral("review.correct");
        if (payload.contains("historyOnly")&&payload.value("historyOnly").metaType().id()!=QMetaType::Bool) {detail="historyOnly must be a boolean.";return false;}
        const auto review=model::reviewFromMap(object(payload,"review"));
        if (!review) {detail=review.error();return false;}
        if (payload.value("historyOnly").toBool()) {
            if (correction) {detail="A review correction must include its schedule.";return false;}
            return true;
        }
        const auto schedule=model::variantFromMap(object(payload,"variant"));
        if (!schedule) {detail=schedule.error();return false;}
        if (review->cardId!=schedule->cardId||review->variantId!=schedule->id||review->due!=schedule->due) {detail="Review and schedule dependencies do not match.";return false;}
        if (correction) {
            const auto previousReview=model::reviewFromMap(object(payload,"previousReview"));
            const auto previousSchedule=model::variantFromMap(object(payload,"previousVariant"));
            if (!previousReview) {detail=previousReview.error();return false;}
            if (!previousSchedule) {detail=previousSchedule.error();return false;}
            if (review->id!=previousReview->id||review->cardId!=previousReview->cardId||review->variantId!=previousReview->variantId
                ||review->deckName!=previousReview->deckName||review->reviewedAt!=previousReview->reviewedAt||review->responseSeconds!=previousReview->responseSeconds
                ||review->sessionId!=previousReview->sessionId||review->sessionDeckName!=previousReview->sessionDeckName||review->sessionStartedAt!=previousReview->sessionStartedAt
                ||schedule->id!=previousSchedule->id||schedule->cardId!=previousSchedule->cardId||schedule->key!=previousSchedule->key
                ||schedule->reviewCount!=previousSchedule->reviewCount||schedule->reviewCount<1||previousReview->due!=previousSchedule->due) {
                detail="A correction must preserve the review identity, time, response duration, and review count.";return false;
            }
        }
        return true;
    }
    detail="Unsupported sync event type.";return false;
}
}
std::expected<void,QString> validateEvent(const QVariantMap &event)
{
    QString detail;
    if (!validateEventImpl(event,detail)) return std::unexpected(detail);
    return {};
}
std::expected<void,QString> validateResponse(const QVariantMap &response,qint64 oldCursor,const QSet<QString> &submittedIds)
{
    if (oldCursor<0||oldCursor>9007199254740991LL
        ||response.value("acceptedIds").metaType().id()!=QMetaType::QVariantList
        ||response.value("events").metaType().id()!=QMetaType::QVariantList
        ||!integer(response.value("cursor"),oldCursor,9007199254740991LL)
        ||response.value("hasMore").metaType().id()!=QMetaType::Bool)
        return std::unexpected(QStringLiteral("Response fields are missing or have invalid types."));
    if (json(response).toUtf8().size()>8*1024*1024)
        return std::unexpected(QStringLiteral("Response exceeds the 8 MiB transport limit."));
    const QVariantList accepted=response.value("acceptedIds").toList();
    const QVariantList events=response.value("events").toList();
    const qint64 cursor=response.value("cursor").toLongLong();
    if (accepted.size()>100||events.size()>1000) return std::unexpected(QStringLiteral("Response exceeds its batch limits."));
    if (events.isEmpty()&&response.value("hasMore").toBool())
        return std::unexpected(QStringLiteral("A response with more download pages must contain at least one event."));
    QSet<QString> acceptedIds,eventIds;
    for (const QVariant &id:accepted) {
        if (id.metaType().id()!=QMetaType::QString||!model::validUuid(id.toString())
            ||acceptedIds.contains(id.toString())||!submittedIds.contains(id.toString()))
            return std::unexpected(QStringLiteral("Accepted event identifiers must be unique UUIDs from the issued upload batch."));
        acceptedIds.insert(id.toString());
    }
    qint64 previous=oldCursor;
    for (const QVariant &value:events) {
        if (value.metaType().id()!=QMetaType::QVariantMap)
            return std::unexpected(QStringLiteral("Remote event wrapper must be an object."));
        const QVariantMap wrapper=value.toMap();
        if (!integer(wrapper.value("seq"),1,cursor)||wrapper.value("event").metaType().id()!=QMetaType::QVariantMap)
            return std::unexpected(QStringLiteral("Remote event sequence is invalid."));
        const qint64 seq=wrapper.value("seq").toLongLong();
        const QVariantMap event=object(wrapper,"event");
        if (seq!=previous+1||eventIds.contains(event.value("id").toString()))
            return std::unexpected(QStringLiteral("Remote events must have unique identifiers and cover every sequence after the current cursor without gaps."));
        const auto valid=validateEvent(event);
        if (!valid) return std::unexpected(valid.error());
        previous=seq;eventIds.insert(event.value("id").toString());
    }
    if (previous!=cursor)
        return std::unexpected(QStringLiteral("The download cursor must equal the last covered event sequence, or stay unchanged for an empty batch."));
    return {};
}
}
