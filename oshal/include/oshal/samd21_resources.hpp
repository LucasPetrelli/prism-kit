#ifndef OSHAL_SAMD21_RESOURCES_HPP_
#define OSHAL_SAMD21_RESOURCES_HPP_

#include "oshal/gpio.hpp"
#include "oshal/pwm.hpp"
#include "oshal/ws2812.hpp"

namespace oshal {

/// @brief Physical GPIO resource bound to SAMD21 pin PA17.
extern Gpio& samd21_gpio_pa17;

/// @brief Physical PWM output bound to SAMD21 pin PA8 routed as TCC0/WO[0].
extern PwmOutput& samd21_pwm_pa8_tcc0_wo0;

/// @brief Sequencing-capable view of the SAMD21 PA8 TCC0/WO[0] PWM output.
/// @note This references the same physical output as samd21_pwm_pa8_tcc0_wo0.
extern PwmSequenceOutput& samd21_pwm_pa8_tcc0_wo0_sequence_output;

/// @brief WS2812 transport layered over the SAMD21 PA8 TCC0/WO[0] PWM path.
extern Ws2812Transport& samd21_ws2812_pa8_tcc0_wo0_transport;

}  // namespace oshal

#endif /* OSHAL_SAMD21_RESOURCES_HPP_ */