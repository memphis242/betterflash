#include "scheduler.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>

namespace betterflash::scheduler {
namespace {
constexpr std::array<std::chrono::seconds, 4> learningSteps{
    minimumInterval, std::chrono::hours{1}, std::chrono::hours{8}, std::chrono::days{1}};
constexpr double matureMissRetention = 0.25;

constexpr double stabilityDays(const std::chrono::seconds interval)
{
    return static_cast<double>(interval.count()) / 86400.0;
}

static_assert(learningSteps.front() == minimumInterval);
static_assert(learningSteps[0] < learningSteps[1] && learningSteps[1] < learningSteps[2]
              && learningSteps[2] < learningSteps[3]);
static_assert(stabilityDays(learningSteps.back()) == 1.0);
static_assert(matureMissRetention > 0.0 && matureMissRetention < 1.0);
}

struct ReviewResult schedule(const double stability, const double difficulty, const int grade,
                             const double recallFraction, const double responseSeconds,
                             const int reviewCount, const Phase phase, const double readingBudgetSeconds)
{
    assert(std::isfinite(stability));
    assert(std::isfinite(difficulty));
    assert(std::isfinite(recallFraction));
    assert(std::isfinite(responseSeconds));
    assert(reviewCount >= 0);
    assert(grade >= 0 && grade <= 4);
    assert(phase == Phase::Learning || phase == Phase::Daily);
    assert(phase != Phase::Daily || reviewCount > 0);
    assert(std::isfinite(readingBudgetSeconds) && readingBudgetSeconds > 0);
    const double oldStability = std::clamp(stability, minimumStabilityDays, maximumStabilityDays);
    const double oldDifficulty = std::clamp(difficulty, 0.0, 1.0);
    const double recall = std::clamp(recallFraction, 0.0, 1.0);
    const double latency = std::clamp(responseSeconds, 0.0, 3600.0);
    const double readingAllowance = std::clamp(readingBudgetSeconds, 1.0, 180.0);
    const double latencyFactor = std::clamp(readingAllowance / std::max(readingAllowance, latency), 0.7, 1.0);
    struct ReviewResult result;
    double multiplier = 1.0;
    switch (grade) {
    case 0:
        result.difficulty = std::clamp(oldDifficulty + 0.16, 0.0, 1.0);
        break;
    case 1:
        multiplier = 0.65 + recall * 0.45;
        result.difficulty = std::clamp(oldDifficulty + 0.07 - recall * 0.06, 0.0, 1.0);
        break;
    case 2:
        multiplier = 1.05 + recall * 0.3;
        result.difficulty = std::clamp(oldDifficulty + 0.03 - recall * 0.04, 0.0, 1.0);
        break;
    case 3:
        multiplier = 1.35 + recall * 0.55;
        result.difficulty = std::clamp(oldDifficulty - 0.035 - recall * 0.035, 0.0, 1.0);
        break;
    default:
        assert(grade == 4);
        multiplier = 1.7 + recall * 0.8;
        result.difficulty = std::clamp(oldDifficulty - 0.07 - recall * 0.05, 0.0, 1.0);
        break;
    }
    if (phase == Phase::Learning && (reviewCount == 0 || grade == 0)) {
        result.stability = minimumStabilityDays;
        result.interval = minimumInterval;
    } else if (phase == Phase::Learning) {
        const double learningStability = std::min(oldStability, 1.0);
        const auto current = std::lower_bound(learningSteps.cbegin(), learningSteps.cend(), learningStability,
            [](const std::chrono::seconds step, const double value) { return stabilityDays(step) < value; });
        assert(current != learningSteps.cend());
        const auto next = grade >= 3 && current + 1 != learningSteps.cend() ? current + 1 : current;
        result.interval = *next;
        result.stability = stabilityDays(result.interval);
        result.graduated = result.interval >= std::chrono::days{1};
    } else {
        const double difficultyFactor = 1.15 - 0.3 * oldDifficulty;
        result.stability = std::clamp(grade == 0 ? oldStability * matureMissRetention
                                              : oldStability * multiplier * latencyFactor * difficultyFactor,
                                      1.0, maximumStabilityDays);
        const auto days = static_cast<int>(std::floor(result.stability));
        assert(days >= 1);
        result.interval = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::days{days});
        result.graduated = true;
    }
    assert(std::isfinite(result.stability));
    assert(result.stability >= minimumStabilityDays && result.stability <= maximumStabilityDays);
    assert(result.difficulty >= 0.0 && result.difficulty <= 1.0);
    assert(result.interval >= minimumInterval && result.interval <= std::chrono::days{3650});
    return result;
}

}
