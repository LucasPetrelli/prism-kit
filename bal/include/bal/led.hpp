#ifndef BAL_LED_HPP_
#define BAL_LED_HPP_

#include "bal/led.h"

namespace bal {

using Led = bal_led_t;

/// @brief Initialize BAL-owned LED objects through the stable C ABI.
/// @return 0 on success, or a negative errno-style code on failure.
inline int initialize_leds()
{
	return bal_leds_init();
}

/// @brief Return the board-owned status LED handle.
/// @return Pointer to the stable status LED handle.
inline const Led *status_led()
{
	return bal_status_led();
}

/// @brief Set the logical state of a board-owned LED.
/// @param led Reference to a board-owned LED handle.
/// @param on True drives the logical LED state on, false drives it off.
/// @return 0 on success, or a negative errno-style code on failure.
inline int set_led(const Led &led, bool on)
{
	return bal_led_set(&led, on);
}

/// @brief Toggle the logical state of a board-owned LED.
/// @param led Reference to a board-owned LED handle.
/// @return 0 on success, or a negative errno-style code on failure.
inline int toggle_led(const Led &led)
{
	return bal_led_toggle(&led);
}

} // namespace bal

#endif /* BAL_LED_HPP_ */