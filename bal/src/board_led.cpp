#include "bal/led.hpp"
#include "oshal/gpio.hpp"
#include "oshal/status.h"
#include "board_led_internal.hpp"

namespace bal::internal {

LedStatus::LedStatus(const char *led_name, const oshal::Gpio &gpio, bool is_active_low)
	: led_name_(led_name), gpio_(gpio), is_active_low_(is_active_low), is_initialized_(false)
{
}

const char *LedStatus::name() const
{
	return led_name_;
}

bool LedStatus::is_ready() const
{
	return gpio_.is_ready();
}

int LedStatus::initialize()
{
	if (is_initialized_) {
		return STATUS_OK;
	}

	if (!is_ready()) {
		return STATUS_ERR_DEVICE_UNAVAILABLE;
	}

	const int ret = gpio_.configure_output(output_level_for_state(false));
	if (ret < 0) {
		return ret;
	}

	is_initialized_ = true;
	return STATUS_OK;
}

int LedStatus::set(bool on) const
{
	if (!is_initialized_) {
		return STATUS_ERR_NOT_READY;
	}

	return gpio_.set(output_level_for_state(on));
}

int LedStatus::toggle() const
{
	if (!is_initialized_) {
		return STATUS_ERR_NOT_READY;
	}

	return gpio_.toggle();
}

bool LedStatus::output_level_for_state(bool on) const
{
	return is_active_low_ ? !on : on;
}

} // namespace bal::internal

namespace bal {

int initialize_leds()
{
	return internal::status_led_backend().initialize();
}

Led &status_led()
{
	return internal::status_led_backend();
}

} // namespace bal
