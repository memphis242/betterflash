#include "checkfollowuptags.h"

#include <QStringList>

namespace betterflash::checkfollowup {
QString withoutTags(const QString &tags)
{
    QStringList retained;
    bool removed=false;
    for (const QString &part : tags.split(QLatin1Char(','))) {
        const QString value=part.trimmed();
        if (value==QStringLiteral("check-correctness") || value==QStringLiteral("check-completeness"))
            removed=true;
        else
            retained.append(part);
    }
    return removed ? retained.join(QLatin1Char(',')) : tags;
}
}
