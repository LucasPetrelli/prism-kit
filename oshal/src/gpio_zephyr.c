#include <stddef.h>

#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>

#include "oshal/gpio.h"
#include "oshal/status.h"

#define GPIO_PA17_PORT_NODE DT_NODELABEL(porta)
#define GPIO_PA17_PIN 17U

#if !DT_NODE_HAS_STATUS_OKAY(GPIO_PA17_PORT_NODE)
#error "Unsupported board: GPIO controller for PA17 is not available"
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
	[OSHAL_GPIO_PIN_PA17] = {
		.name = "PA17",
		.spec = {
			.port = DEVICE_DT_GET(GPIO_PA17_PORT_NODE),
			.pin = GPIO_PA17_PIN,
			.dt_flags = 0U,
		},
	},
};

static const struct oshal_gpio_binding *oshal_gpio_lookup(oshal_gpio_pin_id_t pin_id)
{
	/* Reject invalid physical identifiers before touching the backend table. */
	if ((pin_id < 0) || (pin_id >= OSHAL_GPIO_PIN_COUNT)) {
		return NULL;
	}

	return &oshal_gpio_bindings[pin_id];
}

static int oshal_status_from_backend_result(int backend_result)
{
	if (backend_result < 0) {
		return STATUS_ERR_BACKEND;
	}

	return STATUS_OK;
}

bool oshal_gpio_pin_is_ready(oshal_gpio_pin_id_t pin_id)
{
	const struct oshal_gpio_binding *binding = oshal_gpio_lookup(pin_id);

	if (binding == NULL) {
		return false;
	}

	return gpio_is_ready_dt(&binding->spec);
}

int oshal_gpio_pin_configure_output(oshal_gpio_pin_id_t pin_id, bool initial_high)
{
	const struct oshal_gpio_binding *binding = oshal_gpio_lookup(pin_id);

	if (binding == NULL) {
		return STATUS_ERR_INVALID_ARGUMENT;
	}

	if (!gpio_is_ready_dt(&binding->spec)) {
		return STATUS_ERR_DEVICE_UNAVAILABLE;
	}

	return oshal_status_from_backend_result(gpio_pin_configure(
		binding->spec.port,
		binding->spec.pin,
		(initial_high ? GPIO_OUTPUT_HIGH : GPIO_OUTPUT_LOW) | binding->spec.dt_flags));
}

int oshal_gpio_pin_set(oshal_gpio_pin_id_t pin_id, bool high)
{
	const struct oshal_gpio_binding *binding = oshal_gpio_lookup(pin_id);

	if (binding == NULL) {
		return STATUS_ERR_INVALID_ARGUMENT;
	}

	if (!gpio_is_ready_dt(&binding->spec)) {
		return STATUS_ERR_DEVICE_UNAVAILABLE;
	}

	return oshal_status_from_backend_result(
		gpio_pin_set(binding->spec.port, binding->spec.pin, high ? 1 : 0));
}

int oshal_gpio_pin_toggle(oshal_gpio_pin_id_t pin_id)
{
	const struct oshal_gpio_binding *binding = oshal_gpio_lookup(pin_id);

	if (binding == NULL) {
		return STATUS_ERR_INVALID_ARGUMENT;
	}

	if (!gpio_is_ready_dt(&binding->spec)) {
		return STATUS_ERR_DEVICE_UNAVAILABLE;
	}

	return oshal_status_from_backend_result(gpio_pin_toggle(binding->spec.port, binding->spec.pin));
}

const char *oshal_gpio_pin_name(oshal_gpio_pin_id_t pin_id)
{
	const struct oshal_gpio_binding *binding = oshal_gpio_lookup(pin_id);

	if (binding == NULL) {
		return "invalid_pin";
	}

	return binding->name;
}