#pragma once

#include "schedulelimits.h"

namespace betterflash::scheduler {

enum class Phase { Learning, Daily };

struct ReviewResult {
    double stability = minimumStabilityDays;
    double difficulty = 0.5;
    std::chrono::seconds interval = minimumInterval;
    bool graduated = false;
};

// Adaptive v1 is a bounded heuristic. It is intended to be inspectable and
// predictable, and has not been clinically or scientifically validated.
struct ReviewResult schedule(double stability, double difficulty, int grade,
                      double recallFraction, double responseSeconds,
                      int reviewCount, Phase phase, double readingBudgetSeconds = 12.0);

}
