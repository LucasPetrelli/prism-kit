#ifndef PRISM_TIME_HPP_
#define PRISM_TIME_HPP_

#include <cstdint>

namespace prism {

/// @brief Sleep for the requested number of milliseconds.
/// @param duration_ms Duration to sleep, in milliseconds.
void SleepMs(std::uint32_t duration_ms);

/// @brief Return the system uptime in milliseconds.
/// @return Monotonically-increasing uptime since boot, in milliseconds.
std::uint32_t UptimeMs();

}  // namespace prism

#endif /* PRISM_TIME_HPP_ */