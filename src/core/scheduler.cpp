#include "scheduler.h"

#include <algorithm>
#include <cassert>
#include <cmath>

namespace betterflash::scheduler {

struct ReviewResult schedule(double stability, double difficulty, int grade,
                      double recallFraction, double responseSeconds,
                      int reviewCount, double readingBudgetSeconds)
{
    assert(std::isfinite(stability));
    assert(std::isfinite(difficulty));
    assert(std::isfinite(recallFraction));
    assert(std::isfinite(responseSeconds));
    assert(reviewCount >= 0);
    assert(grade >= 0 && grade <= 4);
    assert(std::isfinite(readingBudgetSeconds) && readingBudgetSeconds > 0);
    const double oldStability = std::clamp(stability, 0.25, 3650.0);
    const double oldDifficulty = std::clamp(difficulty, 0.0, 1.0);
    const double recall = std::clamp(recallFraction, 0.0, 1.0);
    const double latency = std::clamp(responseSeconds, 0.0, 3600.0);
    const double readingAllowance = std::clamp(readingBudgetSeconds, 1.0, 180.0);
    const double latencyFactor = std::clamp(readingAllowance / std::max(readingAllowance, latency), 0.7, 1.0);
    struct ReviewResult result;
    switch (std::clamp(grade, 0, 4)) {
    case 0:
        result.stability = 0.25;
        result.intervalDays = 1;
        result.difficulty = std::clamp(oldDifficulty + 0.16, 0.0, 1.0);
        break;
    case 1:
        result.stability = std::clamp(oldStability * (0.65 + recall * 0.45) * latencyFactor, 0.25, 3650.0);
        result.intervalDays = std::max(1, static_cast<int>(std::floor(result.stability)));
        result.difficulty = std::clamp(oldDifficulty + 0.07 - recall * 0.06, 0.0, 1.0);
        break;
    case 2:
        result.stability = std::clamp(oldStability * (1.05 + recall * 0.3) * latencyFactor, 0.25, 3650.0);
        result.intervalDays = std::max(1, static_cast<int>(std::floor(result.stability)));
        result.difficulty = std::clamp(oldDifficulty + 0.03 - recall * 0.04, 0.0, 1.0);
        break;
    case 3:
        result.stability = std::clamp(oldStability * (1.35 + recall * 0.55) * latencyFactor, 0.25, 3650.0);
        result.intervalDays = std::max(1, static_cast<int>(std::floor(result.stability)));
        result.difficulty = std::clamp(oldDifficulty - 0.035 - recall * 0.035, 0.0, 1.0);
        break;
    default:
        result.stability = std::clamp(oldStability * (1.7 + recall * 0.8) * latencyFactor, 0.25, 3650.0);
        result.intervalDays = std::max(1, static_cast<int>(std::floor(result.stability)));
        result.difficulty = std::clamp(oldDifficulty - 0.07 - recall * 0.05, 0.0, 1.0);
        break;
    }
    if (grade>0) {
        const double difficultyFactor=1.15-0.3*oldDifficulty;
        result.stability=std::clamp(result.stability*difficultyFactor,0.25,3650.0);
        result.intervalDays=std::max(1,static_cast<int>(std::floor(result.stability)));
    }
    if (reviewCount == 0)
        result.intervalDays = std::min(result.intervalDays, grade == 0 ? 1 : 2);
    return result;
}

}
