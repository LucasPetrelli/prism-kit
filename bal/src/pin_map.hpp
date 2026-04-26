#ifndef BAL_PIN_MAP_HPP_
#define BAL_PIN_MAP_HPP_

#include "oshal/gpio.hpp"
#include "oshal/pwm.hpp"
#include "oshal/ws2812.hpp"

namespace bal::internal::pin_map {

/// @brief Return the GPIO resource wired to the board status indicator.
/// @return Reference to the board-selected physical GPIO.
oshal::Gpio& status_gpio();

/// @brief Return the PWM output that drives the board strip waveform path.
/// @return Reference to the board-selected physical PWM output.
oshal::PwmOutput& strip_pwm_output();

/// @brief Return the sequencing-capable PWM output for the board strip path.
/// @return Reference to the board-selected sequence-capable PWM output.
oshal::PwmSequenceOutput& strip_pwm_sequence_output();

/// @brief Return the WS2812 transport wired to the board strip.
/// @return Reference to the board-selected WS2812 transport.
oshal::Ws2812Transport& strip_ws2812_transport();

}  // namespace bal::internal::pin_map

#endif /* BAL_PIN_MAP_HPP_ */