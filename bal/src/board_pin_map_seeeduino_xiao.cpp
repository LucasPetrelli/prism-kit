#include "oshal/samd21_resources.hpp"
#include "pin_map.hpp"

namespace bal::internal::pin_map {

oshal::Gpio& status_gpio() { return oshal::samd21_gpio_pa17; }

oshal::PwmOutput& strip_pwm_output() { return oshal::samd21_pwm_pa8_tcc0_wo0; }

oshal::PwmSequenceOutput& strip_pwm_sequence_output() {
  return oshal::samd21_pwm_pa8_tcc0_wo0_sequence_output;
}

oshal::Ws2812Transport& strip_ws2812_transport() {
  return oshal::samd21_ws2812_pa8_tcc0_wo0_transport;
}

}  // namespace bal::internal::pin_map