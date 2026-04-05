#include <climits>

#include <zephyr/drivers/uart.h>
#include <zephyr/sys/cbprintf.h>

#include "oshal/status.h"
#include "debug_port_zephyr_internal.hpp"

namespace {

/*
 * Zephyr's cbprintf formatter emits one character at a time through a C-style
 * callback. Keep that callback file-local so the generic Zephyr-backed debug
 * port can stream formatted output without allocating a temporary string buffer
 * and without exporting another symbol from this translation unit.
 *
 * This helper is intentionally only for the generic fallback path. The CDC ACM
 * transport-specific backend below overrides that behavior and formats into a
 * small chunk buffer first so it can hand larger spans to its FIFO-driven write
 * path instead of pushing one byte at a time through poll_out().
 */
int debug_port_char_out(int c, void *context)
{
	const auto *device = static_cast<const struct device *>(context);

	/*
	 * The generic backend stays on the minimal synchronous UART primitive because
	 * it must work with whatever device the board selects as zephyr,console. That
	 * keeps this path transport-agnostic and makes it a safe fallback for plain
	 * hardware UART consoles even though it is not the most efficient choice for
	 * CDC ACM.
	 */
	uart_poll_out(device, static_cast<unsigned char>(c));
	return c;
}

} // namespace

namespace oshal::internal {

ZephyrDebugPort::ZephyrDebugPort(const char *port_name, const device *device)
	: name_(port_name), device_(device), blocking_poll_mode_checked_(false),
	  blocking_poll_mode_enabled_(false)
{
}

const char *ZephyrDebugPort::name() const
{
	return name_;
}

bool ZephyrDebugPort::is_ready() const
{
	return (device_ != nullptr) && device_is_ready(device_);
}

int ZephyrDebugPort::enable_blocking_poll_mode() const
{
	if (blocking_poll_mode_checked_) {
		return blocking_poll_mode_enabled_ ? STATUS_OK : STATUS_ERR_BACKEND;
	}

	blocking_poll_mode_checked_ = true;

	/*
	 * The chosen CDC ACM UART does not implement the async uart_tx() API, so the
	 * best available way to avoid a pure polling loop is to enable the driver's
	 * flow-control mode. In the CDC ACM driver that changes poll_out() behavior
	 * from "try once and potentially drop" into "sleep while the TX ring is full
	 * and resume when space returns". That is still not a transport-specialized
	 * path, but it avoids burning CPU in a tight loop when USB backpressure shows
	 * up.
	 *
	 * This is intentionally cached after the first successful or failed attempt so
	 * the generic backend does not reconfigure the console device on every write.
	 */
	struct uart_config config;
	if (uart_config_get(device_, &config) != 0) {
		return STATUS_ERR_BACKEND;
	}

	if (config.flow_ctrl == UART_CFG_FLOW_CTRL_RTS_CTS) {
		blocking_poll_mode_enabled_ = true;
		return STATUS_OK;
	}

	config.flow_ctrl = UART_CFG_FLOW_CTRL_RTS_CTS;
	if (uart_configure(device_, &config) != 0) {
		return STATUS_ERR_BACKEND;
	}

	blocking_poll_mode_enabled_ = true;
	return STATUS_OK;
}

int ZephyrDebugPort::write(const char *buffer, std::size_t length) const
{
	if (buffer == nullptr) {
		return length == 0U ? STATUS_OK : STATUS_ERR_INVALID_ARGUMENT;
	}

	if (!is_ready()) {
		return STATUS_ERR_DEVICE_UNAVAILABLE;
	}

	if (enable_blocking_poll_mode() < 0) {
		return STATUS_ERR_BACKEND;
	}

	/*
	 * Preserve write() as the transport primitive for the object. Higher layers
	 * can send already-formatted data through the same path later without taking
	 * a dependency on printf-style formatting. In the generic backend that still
	 * means per-byte poll_out() writes, because the only promise this class can
	 * safely make is "works on any Zephyr UART-like console device".
	 */
	for (std::size_t index = 0; index < length; ++index) {
		uart_poll_out(device_, static_cast<unsigned char>(buffer[index]));
	}

	return STATUS_OK;
}

int ZephyrDebugPort::vprintf(const char *format, std::va_list args) const
{
	if (format == nullptr) {
		return STATUS_ERR_INVALID_ARGUMENT;
	}

	if (!is_ready()) {
		return STATUS_ERR_DEVICE_UNAVAILABLE;
	}

	if (enable_blocking_poll_mode() < 0) {
		return STATUS_ERR_BACKEND;
	}

	/*
	 * Zephyr exposes cbprintf through a C callback typedef that erases the exact
	 * function signature in C++. Route formatting through the low-level callback
	 * formatter anyway so we avoid temporary buffers and keep the generic backend
	 * aligned with the selected console transport. The reinterpret_cast here is a
	 * consequence of that C callback typedef, not an attempt to bypass type safety
	 * elsewhere in the design.
	 */
	const int format_result = z_cbvprintf_impl(reinterpret_cast<cbprintf_cb>(debug_port_char_out),
		const_cast<device *>(device_), format, args, 0U);
	if (format_result < 0) {
		return STATUS_ERR_BACKEND;
	}

	return STATUS_OK;
}

} // namespace oshal::internal