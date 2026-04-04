#ifndef OSHAL_GPIO_H_
#define OSHAL_GPIO_H_

#include <stdbool.h>

#include "status/status.h"

#ifdef __cplusplus
extern "C" {
#endif

/// @brief Logical GPIO signal identifiers exposed above the Zephyr backend.
/// @note These identifiers are stable contracts for upper layers; the backend
///     is free to remap them to different physical pins later.
typedef enum oshal_gpio_signal_id {
	/// @brief Board-owned status LED logical signal.
	OSHAL_GPIO_SIGNAL_STATUS_LED = 0,
	/// @brief Number of logical signals currently defined by OSHAL.
	OSHAL_GPIO_SIGNAL_COUNT,
} oshal_gpio_signal_id_t;

/// @brief Report whether a logical OSHAL GPIO signal is ready to use.
/// @param signal_id Logical signal identifier to query.
/// @return True when the backend for the requested signal is ready, otherwise
///     false.
bool oshal_gpio_is_ready(oshal_gpio_signal_id_t signal_id);

/// @brief Configure a logical OSHAL GPIO signal as an output.
/// @param signal_id Logical signal identifier to configure.
/// @param active True requests an initially active state, false requests an
///     initially inactive state.
/// @return STATUS_OK on success, or a negative project-defined status code on
///     failure.
int oshal_gpio_configure_output(oshal_gpio_signal_id_t signal_id, bool active);

/// @brief Drive a logical OSHAL GPIO signal to the requested active state.
/// @param signal_id Logical signal identifier to drive.
/// @param active True drives the logical signal active, false drives it
///     inactive.
/// @return STATUS_OK on success, or a negative project-defined status code on
///     failure.
int oshal_gpio_set_active(oshal_gpio_signal_id_t signal_id, bool active);

/// @brief Toggle a logical OSHAL GPIO signal.
/// @param signal_id Logical signal identifier to toggle.
/// @return STATUS_OK on success, or a negative project-defined status code on
///     failure.
int oshal_gpio_toggle(oshal_gpio_signal_id_t signal_id);

/// @brief Return a human-readable name for a logical OSHAL GPIO signal.
/// @param signal_id Logical signal identifier to describe.
/// @return Pointer to a static string naming the signal.
const char *oshal_gpio_signal_name(oshal_gpio_signal_id_t signal_id);

#ifdef __cplusplus
}
#endif

#endif /* OSHAL_GPIO_H_ */