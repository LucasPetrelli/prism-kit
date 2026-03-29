#ifndef OSAL_TIME_HPP_
#define OSAL_TIME_HPP_

#include <cstdint>

#include "osal/time.h"

namespace osal {

/// @brief Sleep for the requested number of milliseconds.
/// @param duration_ms Duration to sleep, in milliseconds.
inline void sleep_ms(std::uint32_t duration_ms)
{
	osal_sleep_ms(duration_ms);
}

} // namespace osal

#endif /* OSAL_TIME_HPP_ */