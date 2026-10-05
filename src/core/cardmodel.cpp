#include "cardmodel.h"
#include "mediabackup.h"

#include <QJsonArray>
#include <QRegularExpression>
#include <QSet>
#include <QUuid>
#include <algorithm>
#include <cassert>
#include <cmath>
#include <limits>

namespace betterflash::model {
namespace {
bool text(const QVariantMap &map, const QString &key, int maximum, bool required = false)
{
    const QVariant value = map.value(key);
    return value.metaType().id() == QMetaType::QString && value.toString().size() <= maximum
        && (!required || !value.toString().trimmed().isEmpty())
        && !value.toString().contains(QChar::Null);
}
bool number(const QVariantMap &map, const QString &key, double low, double high, bool integral = false)
{
    const QVariant value = map.value(key);
    switch (value.metaType().id()) {
    case QMetaType::Double: case QMetaType::Float: case QMetaType::Int:
    case QMetaType::UInt: case QMetaType::LongLong: case QMetaType::ULongLong: break;
    default: return false;
    }
    const double n = value.toDouble();
    return std::isfinite(n) && n >= low && n <= high && (!integral || std::floor(n) == n);
}
QString decodeSeparators(const QString &text)
{
    static const QRegularExpression escapes(QStringLiteral(R"((\\+)::)"));
    QString result;int offset=0;
    auto matches=escapes.globalMatch(text);
    while (matches.hasNext()) {
        const auto match=matches.next();
        result+=text.mid(offset,match.capturedStart()-offset);
        const int slashes=match.capturedLength(1);
        result+=QString(slashes%2==1?slashes/2:slashes,QLatin1Char('\\'))+QStringLiteral("::");
        offset=match.capturedEnd();
    }
    return result+text.mid(offset);
}
QString clozeText(const struct Card &card, const QString &key, bool hide)
{
    const auto parsed = parseCloze(card.front);
    assert(parsed.has_value());
    bool ok = false;
    const int group = key.mid(1).toInt(&ok);
    assert(ok);
    QString result;
    int offset = 0;
    for (const struct ClozePart &part : *parsed) {
        result += card.front.mid(offset, part.start - offset);
        result += hide && part.group == group
            ? QStringLiteral("[%1]").arg(part.hint.isEmpty() ? QStringLiteral("...") : part.hint)
            : part.answer;
        offset = part.start + part.length;
    }
    result += card.front.mid(offset);
    return result;
}
}

QString uuid() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }
bool validUuid(const QString &value)
{
    const QUuid parsed(value);
    return !parsed.isNull() && value == parsed.toString(QUuid::WithoutBraces);
}
QString nowUtc() { return QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs); }
std::expected<QString, QString> utcInstant(const QString &value)
{
    if (value.size() > 40 || !(value.endsWith(QLatin1Char('Z'))
            || QRegularExpression(QStringLiteral("[+-]\\d{2}:\\d{2}$")).match(value).hasMatch()))
        return std::unexpected(QStringLiteral("Timestamp must include a UTC offset."));
    const QDateTime date = QDateTime::fromString(value, Qt::ISODateWithMs);
    if (!date.isValid()) return std::unexpected(QStringLiteral("Timestamp is invalid."));
    return date.toUTC().toString(Qt::ISODateWithMs);
}

std::expected<QList<struct ClozePart>, QString> parseCloze(const QString &source)
{
    QList<struct ClozePart> parts;
    static const QRegularExpression expression(QStringLiteral("^c([1-9][0-9]{0,2})::"));
    int offset = 0;
    while (offset < source.size()) {
        const int open = source.indexOf(QStringLiteral("{{"), offset);
        const int strayClose = source.indexOf(QStringLiteral("}}"), offset);
        if (strayClose >= 0 && (open < 0 || strayClose < open))
            return std::unexpected(QStringLiteral("Cloze has an unmatched closing marker."));
        if (open < 0) break;
        const int close = source.indexOf(QStringLiteral("}}"), open + 2);
        const int nested = source.indexOf(QStringLiteral("{{"), open + 2);
        if (close < 0 || (nested >= 0 && nested < close))
            return std::unexpected(QStringLiteral("Cloze markers must close and cannot be nested."));
        const QString body = source.mid(open + 2, close - open - 2);
        const auto match=expression.match(body);
        if (!match.hasMatch()) return std::unexpected(QStringLiteral("Use {{c1::answer}} or {{c1::answer::hint}}, with groups 1 through 999."));
        const QString contents=body.mid(match.capturedLength());
        int separator=-1;
        for (int candidate=contents.indexOf(QStringLiteral("::"));candidate>=0;candidate=contents.indexOf(QStringLiteral("::"),candidate+2)) {
            int slashes=0;
            for (int i=candidate-1;i>=0&&contents.at(i)==QLatin1Char('\\');--i) ++slashes;
            if (slashes%2==0) {separator=candidate;break;}
        }
        QString answer=separator<0?contents:contents.left(separator);
        QString hint=separator<0?QString():contents.mid(separator+2);
        if (answer.trimmed().isEmpty()||(separator>=0&&hint.trimmed().isEmpty()))
            return std::unexpected(QStringLiteral("A cloze answer and an optional hint cannot be empty."));
        answer=decodeSeparators(answer);hint=decodeSeparators(hint);
        parts.append(ClozePart{open,close+2-open,match.captured(1).toInt(),answer,hint});
        offset = close + 2;
    }
    if (parts.isEmpty()) return std::unexpected(QStringLiteral("A cloze card needs at least one {{c1::answer}} marker."));
    return parts;
}
std::expected<QStringList, QString> variantKeys(const struct Card &card)
{
    if (card.kind == QStringLiteral("basic")) return QStringList{QStringLiteral("forward")};
    if (card.kind == QStringLiteral("reverse")) return QStringList{QStringLiteral("forward"), QStringLiteral("reverse")};
    if (card.kind != QStringLiteral("cloze")) return std::unexpected(QStringLiteral("Unsupported card kind."));
    const auto parts = parseCloze(card.front);
    if (!parts) return std::unexpected(parts.error());
    QSet<int> groups;
    for (const struct ClozePart &part : *parts) groups.insert(part.group);
    QList<int> ordered = groups.values();
    std::sort(ordered.begin(), ordered.end());
    QStringList keys;
    for (const int group : ordered) keys.append(QStringLiteral("c%1").arg(group));
    return keys;
}
QString variantId(const QString &cardId, const QString &key)
{
    assert(validUuid(cardId));
    return QUuid::createUuidV5(QUuid(cardId), key.toUtf8()).toString(QUuid::WithoutBraces);
}
QString question(const struct Card &card, const QString &key)
{
    if (card.kind == QStringLiteral("cloze")) return clozeText(card, key, true);
    return key == QStringLiteral("reverse") ? card.back : card.front;
}
QString answer(const struct Card &card, const QString &key)
{
    if (card.kind != QStringLiteral("cloze")) return key == QStringLiteral("reverse") ? card.front : card.back;
    return clozeText(card, key, false) + (card.back.trimmed().isEmpty() ? QString() : QStringLiteral("\n\n") + card.back);
}
double readingBudget(const QString &question)
{
    const int words = question.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts).size();
    const int mathCharacters = question.count(QLatin1Char('$'));
    const int codeLines = question.contains(QStringLiteral("```")) ? question.count(QLatin1Char('\n')) : 0;
    return std::clamp(4.0 + words / 3.0 + mathCharacters * 1.5 + codeLines * 0.75, 4.0, 180.0);
}
QString gradeLabel(int grade)
{
    assert(grade >= 0 && grade <= 4);
    return QStringList{QStringLiteral("Missed"), QStringLiteral("Partial"), QStringLiteral("Hard"), QStringLiteral("Good"), QStringLiteral("Easy")}.at(grade);
}
QVariantMap toMap(const struct Deck &r)
{ return {{"id",r.id},{"name",r.name},{"description",r.description},{"createdAt",r.createdAt}}; }
QVariantMap toMap(const struct Card &r)
{ return {{"id",r.id},{"deckId",r.deckId},{"kind",r.kind},{"front",r.front},{"back",r.back},{"tags",r.tags},{"pointCount",r.pointCount}}; }
QVariantMap toMap(const struct Variant &r)
{ return {{"id",r.id},{"cardId",r.cardId},{"key",r.key},{"due",r.due},{"reviewCount",r.reviewCount},{"stability",r.stability},{"difficulty",r.difficulty}}; }
QVariantMap toMap(const struct Review &r)
{ return {{"id",r.id},{"cardId",r.cardId},{"variantId",r.variantId},{"deckName",r.deckName},{"grade",r.grade},{"recallFraction",r.recallFraction},{"responseSeconds",r.responseSeconds},{"reviewedAt",r.reviewedAt},{"due",r.due}}; }

std::expected<struct Deck, QString> deckFromMap(const QVariantMap &m)
{
    if (!validUuid(m.value("id").toString()) || !text(m,"name",256,true) || !text(m,"description",65536)
        || !text(m,"createdAt",40,true)) return std::unexpected(QStringLiteral("Deck fields are invalid or exceed their limits."));
    const auto time = utcInstant(m.value("createdAt").toString());
    if (!time) return std::unexpected(time.error());
    return Deck{m.value("id").toString(),m.value("name").toString(),m.value("description").toString(),*time};
}
std::expected<struct Card, QString> cardFromMap(const QVariantMap &m)
{
    if (!validUuid(m.value("id").toString()) || !validUuid(m.value("deckId").toString())
        || !text(m,"kind",16,true) || !text(m,"front",maxTextLength,true) || !text(m,"back",maxTextLength)
        || !text(m,"tags",4096) || !number(m,"pointCount",1,1000,true))
        return std::unexpected(QStringLiteral("Card fields are invalid or exceed their limits."));
    const struct Card card{m.value("id").toString(),m.value("deckId").toString(),m.value("kind").toString(),m.value("front").toString(),m.value("back").toString(),m.value("tags").toString(),m.value("pointCount").toInt()};
    if (card.kind != QStringLiteral("cloze") && card.back.trimmed().isEmpty())
        return std::unexpected(QStringLiteral("A basic or reverse card needs both sides."));
    const auto keys = variantKeys(card);
    if (!keys) return std::unexpected(keys.error());
    const auto references=cardMediaReferences(card);
    if (!references) return std::unexpected(references.error());
    return card;
}
std::expected<QList<struct Card>,QString> atomicCardsFromProposals(const struct Card &source,const QVariantList &proposals)
{
    if (proposals.size()<2||proposals.size()>5)
        return std::unexpected(QStringLiteral("A replacement needs between two and five focused cards."));
    const auto original=cardFromMap(toMap(source));
    if (!original) return std::unexpected(original.error());
    const QStringList originalImages=*cardMediaReferences(source);
    const QSet<QString> allowedImages(originalImages.cbegin(),originalImages.cend());
    QSet<QString> usedImages,questions;
    QList<struct Card> result;
    for (const QVariant &value:proposals) {
        if (value.metaType().id()!=QMetaType::QVariantMap)
            return std::unexpected(QStringLiteral("Each proposed card must contain a question and answer."));
        QVariantMap fields=value.toMap();
        if (!fields.contains("pointCount")) fields.insert("pointCount",1);
        if (!text(fields,"front",10000,true)||!text(fields,"back",10000,true)||!number(fields,"pointCount",1,1000,true))
            return std::unexpected(QStringLiteral("Each proposed side needs Markdown text of at most 10000 characters and a whole answer-point count from 1 through 1000."));
        const struct Card proposed{uuid(),source.deckId,QStringLiteral("basic"),fields.value("front").toString(),
            fields.value("back").toString(),source.tags,fields.value("pointCount").toInt()};
        const auto valid=cardFromMap(toMap(proposed));
        if (!valid) return std::unexpected(valid.error());
        const QString question=proposed.front.simplified().toCaseFolded();
        if (questions.contains(question))
            return std::unexpected(QStringLiteral("Give every proposed card a distinct question."));
        questions.insert(question);
        for (const QString &name:*cardMediaReferences(proposed)) {
            if (!allowedImages.contains(name))
                return std::unexpected(QStringLiteral("A proposed card references an image that is absent from the original card."));
            usedImages.insert(name);
        }
        result.append(proposed);
    }
    if (usedImages!=allowedImages)
        return std::unexpected(QStringLiteral("Keep every attached image on a relevant proposed card before replacing the original."));
    return result;
}
std::expected<struct Variant, QString> variantFromMap(const QVariantMap &m)
{
    if (!validUuid(m.value("id").toString()) || !validUuid(m.value("cardId").toString()) || !text(m,"key",16,true)
        || !text(m,"due",40,true) || !number(m,"reviewCount",0,1000000000,true)
        || !number(m,"stability",0.25,3650) || !number(m,"difficulty",0,1))
        return std::unexpected(QStringLiteral("Variant fields are invalid or exceed their limits."));
    const QString key = m.value("key").toString();
    if (key != QStringLiteral("forward") && key != QStringLiteral("reverse")
        && !QRegularExpression(QStringLiteral("^c[1-9][0-9]{0,2}$")).match(key).hasMatch())
        return std::unexpected(QStringLiteral("Variant key is invalid."));
    const auto due = utcInstant(m.value("due").toString());
    if (!due) return std::unexpected(due.error());
    if (m.value("id").toString() != variantId(m.value("cardId").toString(),key))
        return std::unexpected(QStringLiteral("Variant identifier does not match its source note and key."));
    return Variant{m.value("id").toString(),m.value("cardId").toString(),key,*due,m.value("reviewCount").toInt(),m.value("stability").toDouble(),m.value("difficulty").toDouble()};
}
std::expected<struct Review, QString> reviewFromMap(const QVariantMap &m)
{
    if (!validUuid(m.value("id").toString()) || !validUuid(m.value("cardId").toString())
        || !validUuid(m.value("variantId").toString()) || !text(m,"deckName",256,true)
        || !number(m,"grade",0,4,true) || !number(m,"recallFraction",0,1)
        || !number(m,"responseSeconds",0,86400) || !text(m,"reviewedAt",40,true) || !text(m,"due",40,true))
        return std::unexpected(QStringLiteral("Review fields are invalid or exceed their limits."));
    const auto reviewed = utcInstant(m.value("reviewedAt").toString());
    const auto due = utcInstant(m.value("due").toString());
    if (!reviewed || !due) return std::unexpected(QStringLiteral("Review timestamp is invalid."));
    return Review{m.value("id").toString(),m.value("cardId").toString(),m.value("variantId").toString(),m.value("deckName").toString(),m.value("grade").toInt(),m.value("recallFraction").toDouble(),m.value("responseSeconds").toDouble(),*reviewed,*due};
}
std::expected<struct Collection, QString> collectionFromJson(const QJsonObject &object)
{
    if (object.value("version").toInt() != 2 || !object.value("decks").isArray()
        || !object.value("cards").isArray() || !object.value("variants").isArray() || !object.value("history").isArray())
        return std::unexpected(QStringLiteral("Expected a BetterFlash version 2 collection with decks, cards, variants, and history arrays."));
    int count = 0;
    for (const QString &key : {QStringLiteral("decks"),QStringLiteral("cards"),QStringLiteral("variants"),QStringLiteral("history")}) {
        count += object.value(key).toArray().size();
        if (count > maxCollectionRecords) return std::unexpected(QStringLiteral("Collection exceeds 100000 records."));
    }
    struct Collection result;
    QSet<QString> deletedCards,deletedVariants,deletedDecks;
    if (object.contains("deleted") && !object.value("deleted").isArray())
        return std::unexpected(QStringLiteral("Deleted records must be an array."));
    if (object.value("deleted").toArray().size()+count>maxCollectionRecords)
        return std::unexpected(QStringLiteral("Collection exceeds 100000 records."));
    QSet<QString> deletionKeys;
    for (const QJsonValue &value : object.value("deleted").toArray()) {
        if (!value.isObject()) return std::unexpected(QStringLiteral("Deleted record must be an object."));
        const QString type=value.toObject().value("type").toString();
        const QString id=value.toObject().value("id").toString();
        const QString key=type+QLatin1Char(':')+id;
        if (!validUuid(id)||!QStringList{"deck","card","variant"}.contains(type)||deletionKeys.contains(key))
            return std::unexpected(QStringLiteral("Deleted record has an invalid or duplicate identifier."));
        deletionKeys.insert(key);
        if (type==QStringLiteral("deck")) deletedDecks.insert(id);
        else if (type==QStringLiteral("card")) deletedCards.insert(id);
        else deletedVariants.insert(id);
        result.deleted.append(DeletedRecord{type,id});
    }
    QSet<QString> deckIds,cardIds,variantIds,reviewIds;
    QHash<QString,struct Card> cards;
    QHash<QString,struct Variant> variants;
    for (const QJsonValue &value : object.value("decks").toArray()) {
        if (!value.isObject()) return std::unexpected(QStringLiteral("Deck record must be an object."));
        const auto record = deckFromMap(value.toObject().toVariantMap());
        if (!record) return std::unexpected(record.error());
        if (deletedDecks.contains(record->id)||deckIds.contains(record->id)) return std::unexpected(QStringLiteral("Duplicate deck identifier."));
        deckIds.insert(record->id);result.decks.append(*record);
    }
    for (const QJsonValue &value : object.value("cards").toArray()) {
        if (!value.isObject()) return std::unexpected(QStringLiteral("Card record must be an object."));
        const auto record = cardFromMap(value.toObject().toVariantMap());
        if (!record) return std::unexpected(record.error());
        if (deletedCards.contains(record->id)||cardIds.contains(record->id) || !deckIds.contains(record->deckId))
            return std::unexpected(QStringLiteral("Duplicate card identifier or missing deck."));
        cardIds.insert(record->id);cards.insert(record->id,*record);result.cards.append(*record);
    }
    for (const QJsonValue &value : object.value("variants").toArray()) {
        if (!value.isObject()) return std::unexpected(QStringLiteral("Variant record must be an object."));
        const auto record = variantFromMap(value.toObject().toVariantMap());
        if (!record) return std::unexpected(record.error());
        if (deletedVariants.contains(record->id)||variantIds.contains(record->id) || !cardIds.contains(record->cardId)
            || !variantKeys(cards.value(record->cardId))->contains(record->key))
            return std::unexpected(QStringLiteral("Duplicate variant identifier or invalid source note dependency."));
        variantIds.insert(record->id);variants.insert(record->id,*record);result.variants.append(*record);
    }
    for (const struct Card &card : result.cards) {
        for (const QString &key : *variantKeys(card)) {
            if (!variantIds.contains(variantId(card.id,key)))
                return std::unexpected(QStringLiteral("Source note is missing a review variant."));
        }
    }
    for (const QJsonValue &value : object.value("history").toArray()) {
        if (!value.isObject()) return std::unexpected(QStringLiteral("History record must be an object."));
        const auto record = reviewFromMap(value.toObject().toVariantMap());
        if (!record) return std::unexpected(record.error());
        if (reviewIds.contains(record->id)) return std::unexpected(QStringLiteral("Duplicate review identifier."));
        // Deleted note and variant identifiers authorize retained historical references.
        if ((!cardIds.contains(record->cardId)&&!deletedCards.contains(record->cardId))
            || (!variantIds.contains(record->variantId)&&!deletedVariants.contains(record->variantId)&&!deletedCards.contains(record->cardId)))
            return std::unexpected(QStringLiteral("Review refers to an unknown source note or variant."));
        if (variantIds.contains(record->variantId) && variants.value(record->variantId).cardId != record->cardId)
            return std::unexpected(QStringLiteral("Review source note and variant do not match."));
        reviewIds.insert(record->id);result.reviews.append(*record);
    }
    if (object.contains("media")&&!object.value("media").isArray())
        return std::unexpected(QStringLiteral("Media entries must be an array."));
    if (count+result.deleted.size()+object.value("media").toArray().size()>maxCollectionRecords)
        return std::unexpected(QStringLiteral("Collection exceeds 100000 records."));
    const auto media=mediaFromJson(object.value("media").toArray());
    if (!media) return std::unexpected(media.error());
    const auto references=mediaReferences(result.cards);
    if (!references) return std::unexpected(references.error());
    QSet<QString> mediaNames;for (const struct MediaAttachment &image:*media) mediaNames.insert(image.name);
    for (const QString &name:*references)
        if (!mediaNames.contains(name)) return std::unexpected(QStringLiteral("Referenced image %1 is missing from the backup.").arg(name));
    result.media=*media;
    return result;
}
}
