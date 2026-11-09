#include "cardreadability.h"

#include <QSet>

namespace betterflash::model {
QVariantMap cardReadIssue(const struct Card &card, const QList<struct Variant> &schedules, const QString &metadataError)
{
    const auto issue = [&card](const QString &code, const QString &cause) {
        return QVariantMap{{QStringLiteral("code"), code},
            {QStringLiteral("cardId"), card.id},
            {QStringLiteral("message"), QStringLiteral("This card needs repair. Its saved content and learning history are retained; other cards remain available.")},
            {QStringLiteral("detail"), QStringLiteral("Card %1: %2").arg(card.id.left(128), cause.left(2048))}};
    };
    if (!metadataError.isEmpty()) return issue(QStringLiteral("CARD_METADATA"), metadataError);
    const auto valid = cardFromMap(toMap(card));
    if (!valid) return issue(QStringLiteral("CARD_PARSE"), valid.error());
    const auto supported = variantKeys(card);
    Q_ASSERT(supported);
    QSet<QString> present;
    for (const struct Variant &schedule : schedules) {
        const auto validSchedule = variantFromMap(toMap(schedule));
        if (!validSchedule)
            return issue(QStringLiteral("CARD_SCHEDULE"), QStringLiteral("Review direction %1: %2 Restore a valid backup or explicitly reset this card's schedule after checking its content.")
                .arg(schedule.key.left(128), validSchedule.error()));
        if (schedule.cardId != card.id || !supported->contains(schedule.key))
            return issue(QStringLiteral("CARD_SCHEDULE"), QStringLiteral("Stored direction %1 does not match the card's directions (%2). Review the numbered spans before repairing the card.")
                .arg(schedule.key.left(128), supported->join(QStringLiteral(", "))));
        present.insert(schedule.key);
    }
    for (const QString &key : *supported)
        if (!present.contains(key))
            return issue(QStringLiteral("CARD_SCHEDULE_MISSING"), QStringLiteral("Direction %1 has no saved schedule. Save a repaired card to create that missing direction; existing schedules are retained.").arg(key));
    return {};
}
}
