#include "oshal/gpio.hpp"
#include "board_led_internal.hpp"

namespace {

bal::internal::LedStatus g_status_led{"SeeeduinoXiao.status_led", oshal::pa17, true};

} // namespace

namespace bal::internal {

Led &g_status_led_backend = g_status_led;

} // namespace bal::internal