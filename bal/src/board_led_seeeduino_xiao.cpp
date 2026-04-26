#include "board_led_internal.hpp"
#include "pin_map.hpp"

namespace {

bal::internal::LedStatus& status_led_backend_instance() {
  /*
   * Construct the board LED backend on first use to avoid cross-translation
   * unit static initialization order issues between BAL and OSHAL globals.
   */
  static bal::internal::LedStatus status_led{
    "SeeeduinoXiao.status_led", bal::internal::pin_map::status_gpio(), true};
  return status_led;
}

}  // namespace

namespace bal::internal {

Led& status_led_backend() { return status_led_backend_instance(); }

}  // namespace bal::internal