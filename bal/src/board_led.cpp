#include <array>

#include "bal/led.h"
#include "oshal/gpio.h"
#include "oshal/status.h"

namespace {

struct LedBinding {
	bal_led_t public_led;
	oshal_gpio_pin_id_t pin_id;
	bool is_active_low;
};

constexpr std::size_t kStatusLedIndex = 0U;

const std::array<LedBinding, 1> kLedBindings{{
	LedBinding{{"status_led"}, OSHAL_GPIO_PIN_PA17, true},
}};

bool g_leds_are_initialized = false;

bool led_output_level_for_state(const LedBinding &binding, bool on)
{
	return binding.is_active_low ? !on : on;
}

const LedBinding *lookup_led(const bal_led_t *led)
{
	for (const auto &binding : kLedBindings) {
		if (led == &binding.public_led) {
			return &binding;
		}
	}

	return nullptr;
}

} // namespace

int bal_leds_init(void)
{
	if (g_leds_are_initialized) {
		return 0;
	}

	const auto &status_led = kLedBindings[kStatusLedIndex];
	if (!oshal_gpio_pin_is_ready(status_led.pin_id)) {
		return STATUS_ERR_DEVICE_UNAVAILABLE;
	}

	/* Configure the board-owned status LED once before APP starts using it. */
	const int ret =
		oshal_gpio_pin_configure_output(status_led.pin_id, led_output_level_for_state(status_led, false));
	if (ret < 0) {
		return ret;
	}

	g_leds_are_initialized = true;
	return 0;
}

const bal_led_t *bal_status_led(void)
{
	return &kLedBindings[kStatusLedIndex].public_led;
}

int bal_led_set(const bal_led_t *led, bool on)
{
	if (!g_leds_are_initialized) {
		return STATUS_ERR_NOT_READY;
	}

	const auto *binding = lookup_led(led);
	if (binding == nullptr) {
		return STATUS_ERR_INVALID_ARGUMENT;
	}

	return oshal_gpio_pin_set(binding->pin_id, led_output_level_for_state(*binding, on));
}

int bal_led_toggle(const bal_led_t *led)
{
	if (!g_leds_are_initialized) {
		return STATUS_ERR_NOT_READY;
	}

	const auto *binding = lookup_led(led);
	if (binding == nullptr) {
		return STATUS_ERR_INVALID_ARGUMENT;
	}

	return oshal_gpio_pin_toggle(binding->pin_id);
}