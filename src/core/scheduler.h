#pragma once

#include <QtGlobal>

namespace betterflash::scheduler {

struct ReviewResult {
    double stability = 1.0;
    double difficulty = 0.5;
    int intervalDays = 1;
};

// Adaptive v1 is a bounded heuristic. It is intended to be inspectable and
// predictable, and has not been clinically or scientifically validated.
struct ReviewResult schedule(double stability, double difficulty, int grade,
                      double recallFraction, double responseSeconds,
                      int reviewCount, double readingBudgetSeconds = 12.0);

}
