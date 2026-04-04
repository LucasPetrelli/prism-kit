#include "app/app.hpp"
#include "bal/bootstrap.h"
#include "bal/led.hpp"
#include "oshal/system.h"

int bal_bootstrap_run(void)
{
	/* Refuse to continue if the earlier OSHAL-owned startup stage already failed. */
	if (!oshal_system_ready()) {
		return oshal_system_status();
	}

	/* Bring BAL-owned objects online before APP takes control. */
	const int ret = bal::initialize_leds();
	if (ret < 0) {
		return ret;
	}

	/* BAL owns the transfer into the application layer. */
	return app::run();
}