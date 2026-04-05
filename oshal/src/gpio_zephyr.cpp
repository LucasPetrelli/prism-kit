#include <zephyr/drivers/gpio.h>

#include "oshal/status.h"
#include "gpio_zephyr_internal.hpp"

namespace {

int status_from_backend_result(int backend_result)
{
	if (backend_result < 0) {
		return STATUS_ERR_BACKEND;
	}

	return STATUS_OK;
}

} // namespace

namespace oshal::internal {

ZephyrGpio::ZephyrGpio(const char *gpio_name, const gpio_dt_spec &gpio_spec)
	: name_(gpio_name), spec_(gpio_spec)
{
}

const char *ZephyrGpio::name() const
{
	return name_;
}

bool ZephyrGpio::is_ready() const
{
	return gpio_is_ready_dt(&spec_);
}

int ZephyrGpio::configure_output(bool initial_high) const
{
	if (!gpio_is_ready_dt(&spec_)) {
		return STATUS_ERR_DEVICE_UNAVAILABLE;
	}

	/* Configuration owns the first output level so callers do not need a second write. */
	return status_from_backend_result(gpio_pin_configure(
		spec_.port,
		spec_.pin,
		(initial_high ? GPIO_OUTPUT_HIGH : GPIO_OUTPUT_LOW) | spec_.dt_flags));
}

int ZephyrGpio::set(bool high) const
{
	if (!gpio_is_ready_dt(&spec_)) {
		return STATUS_ERR_DEVICE_UNAVAILABLE;
	}

	/* Keep physical writes normalized to OSHAL status codes before they leave the backend. */
	return status_from_backend_result(gpio_pin_set(spec_.port, spec_.pin, high ? 1 : 0));
}

int ZephyrGpio::toggle() const
{
	if (!gpio_is_ready_dt(&spec_)) {
		return STATUS_ERR_DEVICE_UNAVAILABLE;
	}

	return status_from_backend_result(gpio_pin_toggle(spec_.port, spec_.pin));
}

} // namespace oshal::internal