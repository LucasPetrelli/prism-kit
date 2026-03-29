#include <zephyr/logging/log.h>

#include "bal/bootstrap.h"

LOG_MODULE_REGISTER(main, CONFIG_LOG_DEFAULT_LEVEL);

int main(void)
{
	int ret;

	/*
	 * Keep Zephyr's root entry point in C and thin. BAL owns the next stage and
	 * can bridge into C++ application code without pulling Zephyr concerns upward.
	 */
	ret = bal_bootstrap_run();
	if (ret < 0) {
		LOG_ERR("BAL bootstrap failed: %d", ret);
		return ret;
	}

	return 0;
}
