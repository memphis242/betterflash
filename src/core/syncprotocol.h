#pragma once

#include <QSet>
#include <QString>
#include <QVariantMap>
#include <expected>

namespace betterflash::protocol {
std::expected<void,QString> validateEvent(const QVariantMap &event);
std::expected<void,QString> validateResponse(const QVariantMap &response,qint64 oldCursor,
                                           const QSet<QString> &submittedIds);
}
