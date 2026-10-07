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
bool validateEventImpl(const QVariantMap &event, QString &detail)
{
    if (!model::validUuid(event.value("id").toString())||!model::validUuid(event.value("deviceId").toString())
        ||event.value("payload").metaType().id()!=QMetaType::QVariantMap||!model::utcInstant(event.value("createdAt").toString())
        ||json(event).toUtf8().size()>model::maxSyncEventBytes) {detail="Sync event envelope is invalid or exceeds its size limit.";return false;}
    const QString type=event.value("type").toString();
    const QVariantMap payload=object(event,"payload");
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
