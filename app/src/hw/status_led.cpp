#include "status_led.hpp"

namespace app::hw {

void StatusLed::Configure(bal::Led* led, std::uint32_t idle_sleep_ms) {
  led_ = led;
  blink_half_period_ticks_ = kBlinkHalfPeriodMs / idle_sleep_ms;
  blink_tick_ = 0U;
}

bool StatusLed::Blink() {
  if (led_ == nullptr) {
    return false;
  }

  ++blink_tick_;
  if (blink_tick_ < blink_half_period_ticks_) {
    return true;
  }

  blink_tick_ = 0U;
  return led_->Toggle() >= 0;
}

}  // namespace app::hw
