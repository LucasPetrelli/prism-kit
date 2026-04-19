#include "app/app.hpp"
#include "bal/bootstrap.hpp"

extern "C" int oshal_main_handoff(void) { return bal::run_bootstrap(app::run); }