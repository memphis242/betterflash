#pragma once

#include "cardmodel.h"

namespace betterflash::model {
[[nodiscard]] QVariantMap cardReadIssue(const struct Card &card, const QList<struct Variant> &schedules,
                                      const QString &metadataError = {});
}
