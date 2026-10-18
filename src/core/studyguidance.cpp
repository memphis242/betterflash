#include "studyguidance.h"

#include <QVariantList>
#include <algorithm>
#include <cassert>
#include <cmath>

namespace betterflash::study {
namespace {
QString tier(const double score, const bool assessed)
{
    if (!assessed) return QStringLiteral("unassessed");
    if (score >= 75.0) return QStringLiteral("needs-study");
    if (score >= 50.0) return QStringLiteral("fragile");
    if (score >= 25.0) return QStringLiteral("watch");
    return QStringLiteral("steady");
}

QString label(const QString &value)
{
    if (value == QStringLiteral("needs-study")) return QStringLiteral("Very low");
    if (value == QStringLiteral("fragile")) return QStringLiteral("Low");
    if (value == QStringLiteral("watch")) return QStringLiteral("Moderate");
    if (value == QStringLiteral("steady")) return QStringLiteral("Strong");
    return QStringLiteral("Not assessed");
}
}

QVariantMap assess(const QString &variantId, const QString &variantKey,
                   const double readingBudgetSeconds, const int reviewCount,
                   const QVector<struct ReviewEvidence> &evidence)
{
    assert(std::isfinite(readingBudgetSeconds) && readingBudgetSeconds >= 0.0);
    assert(reviewCount >= 0);
    assert(reviewCount >= evidence.size());
    assert(evidence.size() <= 12);
    const bool assessed = !evidence.isEmpty();
    if (!assessed) {
        return {{"score", 0.0}, {"recallStrength", QVariant{}}, {"tier", QStringLiteral("unassessed")},
                {"label", QStringLiteral("Not assessed")}, {"reviewCount", reviewCount},
                {"sampleCount", 0}, {"provisional", true}, {"reasons", QStringList{}},
                {"recommendation", QStringLiteral("Review this card to establish its recall strength.")},
                {"variantId", variantId}, {"variantKey", variantKey},
                {"factors", QVariantList{}}, {"assessed", false}};
    }

    double weightedLoss = 0.0;
    double weightTotal = 0.0;
    double weightedDifficult = 0.0;
    int difficult = 0;
    int successful = 0;
    QVector<double> successfulTimes;
    successfulTimes.reserve(evidence.size());
    for (qsizetype index = 0; index < evidence.size(); ++index) {
        const struct ReviewEvidence &point = evidence.at(index);
        assert(point.grade >= 0 && point.grade <= 4);
        assert(std::isfinite(point.recall) && point.recall >= 0.0 && point.recall <= 1.0);
        assert(std::isfinite(point.responseSeconds) && point.responseSeconds >= 0.0);
        const double weight = std::pow(0.82, static_cast<double>(index));
        const double gradeLoss = 1.0 - (static_cast<double>(point.grade) / 4.0);
        const double loss = 0.65 * (1.0 - point.recall) + 0.35 * gradeLoss;
        weightedLoss += weight * loss;
        weightTotal += weight;
        if (point.grade <= 2) {
            ++difficult;
            weightedDifficult += weight;
        }
        if (point.grade >= 2 && point.responseSeconds > 0.0) {
            ++successful;
            successfulTimes.append(point.responseSeconds);
        }
    }
    const double lossScore = weightTotal > 0.0 ? weightedLoss / weightTotal : 0.0;
    const double difficultRate = weightTotal > 0.0 ? weightedDifficult / weightTotal : 0.0;
    double deterioration = 0.0;
    if (evidence.size() >= 6) {
        const qsizetype split = evidence.size() / 2;
        double recent = 0.0;
        double older = 0.0;
        for (qsizetype i = 0; i < split; ++i) recent += 1.0 - evidence.at(i).recall;
        for (qsizetype i = split; i < evidence.size(); ++i) older += 1.0 - evidence.at(i).recall;
        recent /= split;
        older /= evidence.size() - split;
        deterioration = std::clamp(recent - older, -1.0, 1.0);
    }

    double speedPenalty = 0.0;
    double successfulBaseline = 0.0;
    double latestSuccessful = 0.0;
    if (successful >= 3) {
        std::sort(successfulTimes.begin(), successfulTimes.end());
        successfulBaseline = successfulTimes.at(successfulTimes.size() / 2);
        for (const struct ReviewEvidence &point : evidence) {
            if (point.grade >= 2 && point.responseSeconds > 0.0) {
                latestSuccessful = point.responseSeconds;
                break;
            }
        }
        const double allowance = std::max({1.0, readingBudgetSeconds * 0.10, successfulBaseline * 0.15});
        speedPenalty = std::clamp((latestSuccessful - successfulBaseline - allowance)
                                       / std::max(1.0, successfulBaseline) * 0.05, 0.0, 0.10);
    }
    const double score = std::clamp(100.0 * (0.70 * lossScore + 0.20 * difficultRate
                                               + 0.10 * std::max(0.0, deterioration) + speedPenalty), 0.0, 100.0);
    const QString weaknessTier = tier(score, true);
    QStringList reasons;
    QVariantList factors;
    factors.append(QVariantMap{{"label", QStringLiteral("Recent recall")}, {"value", 100.0 * lossScore},
                               {"detail", QStringLiteral("Weighted across the latest %1 reviews").arg(evidence.size())}});
    if (difficult > 0) {
        reasons.append(QStringLiteral("%1 of %2 recent reviews were Hard, Partial, or Missed")
                           .arg(difficult).arg(evidence.size()));
        factors.append(QVariantMap{{"label", QStringLiteral("Difficulty")}, {"value", 100.0 * difficultRate},
                                   {"detail", QStringLiteral("%1 of %2 recent reviews were difficult").arg(difficult).arg(evidence.size())}});
    }
    if (deterioration > 0.05) {
        reasons.append(QStringLiteral("Recent recall is deteriorating"));
        factors.append(QVariantMap{{"label", QStringLiteral("Deterioration")}, {"value", 100.0 * deterioration},
                                   {"detail", QStringLiteral("Recent results are weaker than earlier results")}});
    }
    if (speedPenalty > 0.0) {
        reasons.append(QStringLiteral("Successful answers are taking longer than this variant's personal baseline"));
        factors.append(QVariantMap{{"label", QStringLiteral("Response speed")}, {"value", 100.0 * speedPenalty},
                                   {"detail", QStringLiteral("Latest successful response compared with a personal median baseline (%1 s vs %2 s); capped at ten points")
                                       .arg(latestSuccessful, 0, 'f', 1).arg(successfulBaseline, 0, 'f', 1)}});
    }
    if (reasons.isEmpty()) reasons.append(QStringLiteral("Recent recall is holding steady"));
    QString recommendation;
    if (score >= 75.0) {
        recommendation = QStringLiteral("Read one explanation of the topic, write why each step works, then reconstruct the answer without looking.");
    } else if (score >= 50.0) {
        recommendation = QStringLiteral("Explain the answer in your own words and check the prerequisite concepts before trying a similar problem.");
    } else if (score >= 25.0) {
        recommendation = QStringLiteral("Practice one retrieval without looking and identify the step or term that required effort.");
    } else {
        recommendation = QStringLiteral("Keep the scheduled reviews. These observations do not indicate a need for extra study.");
    }
    return {{"score", score}, {"recallStrength", 100.0 - score}, {"tier", weaknessTier}, {"label", label(weaknessTier)},
            {"reviewCount", reviewCount}, {"sampleCount", evidence.size()},
            {"provisional", evidence.size() < 3}, {"reasons", reasons},
            {"recommendation", recommendation}, {"variantId", variantId},
            {"variantKey", variantKey}, {"factors", factors}, {"assessed", true}};
}
}
