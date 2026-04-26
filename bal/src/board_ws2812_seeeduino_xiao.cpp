#include "board_ws2812_internal.hpp"
#include "pin_map.hpp"

namespace {

/* This board owns one fixed seven-pixel strip routed to the PA8 transport. */
bal::internal::BoardWs2812Strip<7U>& ws2812_strip_backend_instance() {
  /*
   * Construct the board strip backend on first use to avoid cross-translation
   * unit static initialization order issues between BAL and OSHAL globals.
   */
  static bal::internal::BoardWs2812Strip<7U> ws2812_strip{
    "SeeeduinoXiao.ws2812_strip",
    bal::internal::pin_map::strip_ws2812_transport(),
    bal::internal::WireColorOrder::kGrb};
  return ws2812_strip;
}

}  // namespace

namespace bal::internal {

Ws2812Strip& ws2812_strip_backend() { return ws2812_strip_backend_instance(); }

}  // namespace bal::internal