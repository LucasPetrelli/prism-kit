#ifndef OSHAL_TIME_HPP_
#define OSHAL_TIME_HPP_

#include <cstdint>

#include "oshal/time.h"

namespace oshal {

/// @brief Sleep for the requested number of milliseconds.
/// @param duration_ms Duration to sleep, in milliseconds.
inline void sleep_ms(std::uint32_t duration_ms) { oshal_sleep_ms(duration_ms); }

}  // namespace oshal

#endif /* OSHAL_TIME_HPP_ */