#include "board_ws2812_internal.hpp"

namespace bal::internal {

/* Pixel views are thin adapters that forward into the owning strip's staged
 * frame buffer.
 */
BoardWs2812Led::BoardWs2812Led(BoardWs2812StripAccess& strip, std::size_t index)
    : strip_(strip), index_(index) {}

bool BoardWs2812Led::is_ready() const { return strip_.strip_is_ready(); }

int BoardWs2812Led::set_color(const RgbColor& color) {
  return strip_.set_pixel_color(index_, color);
}

RgbColor BoardWs2812Led::color() const { return strip_.pixel_color(index_); }

std::size_t BoardWs2812Led::index() const { return index_; }

}  // namespace bal::internal

namespace bal {

/* BAL exposes one board-owned strip backend selected by the board layer. */
int initialize_ws2812_strips() {
  return internal::ws2812_strip_backend().initialize();
}

Ws2812Strip& ws2812_strip() { return internal::ws2812_strip_backend(); }

}  // namespace bal