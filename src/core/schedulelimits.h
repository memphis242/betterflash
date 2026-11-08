#pragma once

#include <chrono>

namespace betterflash::scheduler {

inline constexpr std::chrono::seconds minimumInterval{std::chrono::minutes{10}};
inline constexpr double minimumStabilityDays = static_cast<double>(minimumInterval.count()) / 86400.0;
inline constexpr double maximumStabilityDays = 3650.0;

static_assert(minimumStabilityDays == 1.0 / 144.0);
static_assert(minimumStabilityDays > 0.0 && minimumStabilityDays < 1.0);
static_assert(maximumStabilityDays * 86400.0 < 2147483647.0);

}
