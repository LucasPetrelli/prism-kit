#include "bal/bootstrap.h"
#include "bal/led.hpp"
#include "oshal/status.h"
#include "oshal/system.h"

extern "C" int bal_run(bal_application_entry_t app_entry)
{
	if (app_entry == nullptr) {
		return STATUS_ERR_INVALID_ARGUMENT;
	}

	/* Refuse to continue if the earlier OSHAL-owned startup stage already failed. */
	if (!oshal_system_ready()) {
		return oshal_system_status();
	}

	/* Bring BAL-owned objects online before APP takes control. */
	const int ret = bal::initialize_leds();
	if (ret < 0) {
		return ret;
	}

	/* BAL owns the transfer into the application layer through a supplied C ABI entry point. */
	return app_entry();
}