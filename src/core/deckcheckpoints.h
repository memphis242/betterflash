#pragma once

#include "cardmodel.h"

#include <expected>

namespace betterflash::checkpoints {

inline constexpr qsizetype maxSnapshotBytes = 64 * 1024 * 1024;
inline constexpr int maxNoteLength = 4096;
inline constexpr int maxSummaryLength = 8192;

struct Snapshot {
    QString rootId{};
    QList<struct model::Deck> decks{};
    QList<struct model::Card> cards{};
};

std::expected<struct Snapshot, QString> validateSnapshot(const QVariantMap &value);
QVariantMap snapshotMap(const struct Snapshot &snapshot);
std::expected<void, QString> validateChanges(const QVariantMap &value);
QVariantMap diff(const struct Snapshot &before, const struct Snapshot &after, bool first = false);

}
