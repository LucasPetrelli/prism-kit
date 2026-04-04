#ifndef OSHAL_GPIO_H_
#define OSHAL_GPIO_H_

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/// @brief Physical GPIO pin identifiers exposed by OSHAL.
/// @note OSHAL owns the Zephyr-facing GPIO binding, while higher layers own
///     any board-specific meaning attached to a pin.
typedef enum oshal_gpio_pin_id {
	/// @brief SAMD21 port A pin 17.
	OSHAL_GPIO_PIN_PA17 = 0,
	/// @brief Number of OSHAL GPIO pins currently exposed.
	OSHAL_GPIO_PIN_COUNT,
} oshal_gpio_pin_id_t;

/// @brief Report whether an OSHAL GPIO pin is ready to use.
/// @param pin_id Physical pin identifier to query.
/// @return True when the backend for the requested pin is ready, otherwise
///     false.
bool oshal_gpio_pin_is_ready(oshal_gpio_pin_id_t pin_id);

/// @brief Configure an OSHAL GPIO pin as an output.
/// @param pin_id Physical pin identifier to configure.
/// @param initial_high True drives the pin high after configuration, false
///     drives it low.
/// @return STATUS_OK on success, or a negative project-defined status code on
///     failure.
int oshal_gpio_pin_configure_output(oshal_gpio_pin_id_t pin_id, bool initial_high);

/// @brief Drive an OSHAL GPIO pin to the requested physical level.
/// @param pin_id Physical pin identifier to drive.
/// @param high True drives the pin high, false drives it low.
/// @return STATUS_OK on success, or a negative project-defined status code on
///     failure.
int oshal_gpio_pin_set(oshal_gpio_pin_id_t pin_id, bool high);

/// @brief Toggle an OSHAL GPIO pin.
/// @param pin_id Physical pin identifier to toggle.
/// @return STATUS_OK on success, or a negative project-defined status code on
///     failure.
int oshal_gpio_pin_toggle(oshal_gpio_pin_id_t pin_id);

/// @brief Return a human-readable name for an OSHAL GPIO pin.
/// @param pin_id Physical pin identifier to describe.
/// @return Pointer to a static string naming the pin.
const char *oshal_gpio_pin_name(oshal_gpio_pin_id_t pin_id);

#ifdef __cplusplus
}
#endif

#endif /* OSHAL_GPIO_H_ */