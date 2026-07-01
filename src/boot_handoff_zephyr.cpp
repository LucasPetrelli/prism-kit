#include "app/app.hpp"
#include "bal/bootstrap.hpp"

// NOLINTNEXTLINE(readability-identifier-naming)
extern "C" int oshal_main_handoff(void) {
  return bal::RunBootstrap(app::Setup, app::Loop);
}