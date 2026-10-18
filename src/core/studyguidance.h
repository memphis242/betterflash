#pragma once

#include <QVariantMap>
#include <QVector>

namespace betterflash::study {

struct ReviewEvidence {
    int grade = 0;
    double recall = 0.0;
    double responseSeconds = 0.0;
};

// Returns an explainable study-priority assessment. The score is a review
// signal, not a probability of mastery.
QVariantMap assess(const QString &variantId, const QString &variantKey,
                   double readingBudgetSeconds, int reviewCount,
                   const QVector<struct ReviewEvidence> &evidence);

}
