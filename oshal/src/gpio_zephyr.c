#include <stddef.h>

#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>

#include "oshal/gpio.h"
#include "status/status.h"

#define STATUS_LED_NODE DT_ALIAS(led0)

#if !DT_NODE_HAS_STATUS_OKAY(STATUS_LED_NODE)
#error "Unsupported board: led0 devicetree alias is not defined"
#endif

struct oshal_gpio_binding {
	const char *name;
	struct gpio_dt_spec spec;
};

/*
	 * Keep the hardware-facing signal table in plain C so the devicetree and GPIO
	 * wiring remain easy to inspect during bring-up.
 */
static const struct oshal_gpio_binding oshal_gpio_bindings[] = {
	[OSHAL_GPIO_SIGNAL_STATUS_LED] = {
		.name = "status_led",
		.spec = GPIO_DT_SPEC_GET(STATUS_LED_NODE, gpios),
	},
};

static const struct oshal_gpio_binding *oshal_gpio_lookup(oshal_gpio_signal_id_t signal_id)
{
	/* Reject invalid logical identifiers before touching the backend table. */
	if ((signal_id < 0) || (signal_id >= OSHAL_GPIO_SIGNAL_COUNT)) {
		return NULL;
	}

	return &oshal_gpio_bindings[signal_id];
}

static int oshal_status_from_backend_result(int backend_result)
{
	if (backend_result < 0) {
		return STATUS_ERR_BACKEND;
	}

	return STATUS_OK;
}

bool oshal_gpio_is_ready(oshal_gpio_signal_id_t signal_id)
{
	const struct oshal_gpio_binding *binding = oshal_gpio_lookup(signal_id);

	if (binding == NULL) {
		return false;
	}

	return gpio_is_ready_dt(&binding->spec);
}

int oshal_gpio_configure_output(oshal_gpio_signal_id_t signal_id, bool active)
{
	const struct oshal_gpio_binding *binding = oshal_gpio_lookup(signal_id);

	if (binding == NULL) {
		return STATUS_ERR_INVALID_ARGUMENT;
	}

	if (!gpio_is_ready_dt(&binding->spec)) {
		return STATUS_ERR_DEVICE_UNAVAILABLE;
	}

	return oshal_status_from_backend_result(
		gpio_pin_configure_dt(&binding->spec, active ? GPIO_OUTPUT_ACTIVE : GPIO_OUTPUT_INACTIVE));
}

int oshal_gpio_set_active(oshal_gpio_signal_id_t signal_id, bool active)
{
	const struct oshal_gpio_binding *binding = oshal_gpio_lookup(signal_id);

	if (binding == NULL) {
		return STATUS_ERR_INVALID_ARGUMENT;
	}

	if (!gpio_is_ready_dt(&binding->spec)) {
		return STATUS_ERR_DEVICE_UNAVAILABLE;
	}

	return oshal_status_from_backend_result(gpio_pin_set_dt(&binding->spec, active));
}

int oshal_gpio_toggle(oshal_gpio_signal_id_t signal_id)
{
	const struct oshal_gpio_binding *binding = oshal_gpio_lookup(signal_id);

	if (binding == NULL) {
		return STATUS_ERR_INVALID_ARGUMENT;
	}

	if (!gpio_is_ready_dt(&binding->spec)) {
		return STATUS_ERR_DEVICE_UNAVAILABLE;
	}

	return oshal_status_from_backend_result(gpio_pin_toggle_dt(&binding->spec));
}

const char *oshal_gpio_signal_name(oshal_gpio_signal_id_t signal_id)
{
	const struct oshal_gpio_binding *binding = oshal_gpio_lookup(signal_id);

	if (binding == NULL) {
		return "invalid_signal";
	}

	return binding->name;
}