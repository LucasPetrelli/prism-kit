#ifndef BAL_LED_H_
#define BAL_LED_H_

#include <stdbool.h>

#include "status/status.h"

#ifdef __cplusplus
extern "C" {
#endif

/// @brief Stable board-owned LED handle exposed to upper layers.
/// @note Callers must treat instances as board-owned handles and must not
///     construct or mutate them directly.
typedef struct bal_led {
	/// @brief Human-readable LED name intended for diagnostics and debug output.
	const char *name;
} bal_led_t;

/// @brief Prepare all board-owned LED objects exposed by BAL.
/// @return STATUS_OK on success, or a negative project-defined status code if
///     initialization fails.
/// @post LED operations may be used after a successful return.
int bal_leds_init(void);

/// @brief Retrieve the board's default status LED handle.
/// @return Pointer to the board-owned status LED handle.
const bal_led_t *bal_status_led(void);

/// @brief Drive a BAL LED object using logical on or off semantics.
/// @param led Pointer to a BAL-owned LED handle.
/// @param on True drives the logical LED state on, false drives it off.
/// @return STATUS_OK on success, or a negative project-defined status code on
///     failure.
/// @pre bal_leds_init() completed successfully.
int bal_led_set(const bal_led_t *led, bool on);

/// @brief Toggle a BAL LED object using logical LED semantics.
/// @param led Pointer to a BAL-owned LED handle.
/// @return STATUS_OK on success, or a negative project-defined status code on
///     failure.
/// @pre bal_leds_init() completed successfully.
int bal_led_toggle(const bal_led_t *led);

#ifdef __cplusplus
}
#endif

#endif /* BAL_LED_H_ */