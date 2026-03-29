#include <array>

#include "bal/led.h"
#include "hal/gpio.h"
#include "status/status.h"

namespace {

struct LedBinding {
	bal_led_t public_led;
	hal_gpio_signal_id_t signal_id;
};

constexpr std::size_t kStatusLedIndex = 0U;

const std::array<LedBinding, 1> kLedBindings{{
	LedBinding{{"status_led"}, HAL_GPIO_SIGNAL_STATUS_LED},
}};

bool g_leds_are_initialized = false;

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
	if (!hal_gpio_is_ready(status_led.signal_id)) {
		return STATUS_ERR_DEVICE_UNAVAILABLE;
	}

	/* Configure the board-owned status LED once before APP starts using it. */
	const int ret = hal_gpio_configure_output(status_led.signal_id, false);
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

	return hal_gpio_set_active(binding->signal_id, on);
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

	return hal_gpio_toggle(binding->signal_id);
}