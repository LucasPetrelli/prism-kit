#ifndef APP_HW_STATUS_LED_HPP_
#define APP_HW_STATUS_LED_HPP_

#include <cstdint>

#include "bal/led.hpp"

namespace app::hw {

/// @brief Manages the board status LED blink pattern.
///
/// StatusLed toggles the LED at a fixed 0.5 Hz rate (one full on/off cycle
/// every 2 s) by counting idle-sleep ticks.  The caller drives Blink() once
/// per task-loop iteration; StatusLed counts ticks and only toggles the
/// hardware when the half-period elapses.
class StatusLed {
 public:
  /// @brief Desired status-LED half-period in milliseconds.
  /// @note 1000 ms half-period → toggle every 1 s → 0.5 Hz blink
  ///     (one complete on/off cycle every 2 s).
  static constexpr std::uint32_t kBlinkHalfPeriodMs = 1000U;

  /// @brief Wire the status LED and set the idle-sleep granularity.
  /// @param led             Board-owned status LED (must be ready).
  /// @param idle_sleep_ms   Task-loop idle sleep interval, in milliseconds.
  /// @pre idle_sleep_ms must evenly divide kBlinkHalfPeriodMs so the
  ///     half-period tick count is an exact integer.
  void Configure(bal::Led* led, std::uint32_t idle_sleep_ms);

  /// @brief Advance the blink tick counter and toggle the LED if the
  ///     half-period has elapsed.
  /// @return false only on fatal hardware error from the LED driver.
  /// @pre Configure() has been called with a valid LED pointer.
  bool Blink();

  StatusLed() = default;

 private:
  bal::Led* led_ = nullptr;
  std::uint32_t blink_half_period_ticks_ = 0U;
  std::uint32_t blink_tick_ = 0U;
};

}  // namespace app::hw

#endif /* APP_HW_STATUS_LED_HPP_ */
