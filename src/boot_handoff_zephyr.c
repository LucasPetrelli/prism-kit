#include "app/app.h"
#include "bal/bootstrap.h"

int oshal_main_handoff(void)
{
	return bal_run(app_run);
}