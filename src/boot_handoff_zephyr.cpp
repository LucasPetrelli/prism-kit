#include "app/app.hpp"
#include "bal/bootstrap.hpp"

extern "C" int oshal_main_handoff(void) {
  return bal::RunBootstrap(app::setup, app::loop);
}