#ifndef APP_HW_HW_CONSTANTS_HPP_
#define APP_HW_HW_CONSTANTS_HPP_

#include <cstddef>
#include <cstdint>

namespace app::hw {

/// @brief Idle sleep interval for the HW executor task loop, in milliseconds.
///
/// The loop sleeps for this interval on each iteration when no events are
/// pending.  StatusLed blink timing is derived from this interval so the
/// blink rate stays correct when this value changes.
constexpr std::uint32_t kTaskIdleSleepMs = 10U;

/// @brief Stack size for the app_hw task, in bytes.
constexpr std::size_t kTaskStackSizeBytes = 1024U;

/// @brief Priority for the app_hw task (lower = higher priority).
constexpr int kTaskPriority = 0;

}  // namespace app::hw

#endif /* APP_HW_HW_CONSTANTS_HPP_ */
