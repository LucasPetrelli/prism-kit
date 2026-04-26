#ifndef BAL_WS2812_INTERNAL_HPP_
#define BAL_WS2812_INTERNAL_HPP_

#include <array>
#include <cstddef>
#include <cstdint>
#include <utility>

#include "bal/ws2812_strip.hpp"
#include "oshal/status.h"
#include "oshal/ws2812.hpp"

namespace bal::internal {

/// @brief Wire-component ordering selected by the board layer.
enum class WireColorOrder : std::uint8_t {
  /// @brief Emit pixel bytes in RGB order.
  kRgb,
  /// @brief Emit pixel bytes in GRB order as required by many WS2812 strips.
  kGrb,
};

/// @brief Internal access contract shared between a strip and its pixel views.
class BoardWs2812StripAccess {
 public:
  BoardWs2812StripAccess(const BoardWs2812StripAccess&) = delete;
  BoardWs2812StripAccess& operator=(const BoardWs2812StripAccess&) = delete;
  virtual ~BoardWs2812StripAccess() = default;

  /// @brief Report whether the owning strip transport is ready.
  /// @return True when the strip backend is ready.
  virtual bool strip_is_ready() const = 0;

  /// @brief Update one staged pixel color in the owning strip.
  /// @param index Zero-based pixel index.
  /// @param color Requested logical RGB color.
  /// @return STATUS_OK on success, or a negative project-defined status code on
  ///     failure.
  virtual int set_pixel_color(std::size_t index, const RgbColor& color) = 0;

  /// @brief Read one staged pixel color from the owning strip.
  /// @param index Zero-based pixel index.
  /// @return The staged logical RGB color, or black for an out-of-range index.
  virtual RgbColor pixel_color(std::size_t index) const = 0;

 protected:
  BoardWs2812StripAccess() = default;
};

/// @brief BAL pixel view bound to one index in a board-owned strip.
class BoardWs2812Led final : public Ws2812Led {
 public:
  /// @brief Construct a pixel view for one strip-owned index.
  /// @param strip Strip access bridge that owns the staged frame.
  /// @param index Zero-based pixel index represented by this object.
  BoardWs2812Led(BoardWs2812StripAccess& strip, std::size_t index);

  /// @brief Report whether the owning strip transport is ready.
  /// @return True when the strip backend is ready.
  bool is_ready() const override;

  /// @brief Stage a new logical color for this pixel.
  /// @param color Requested logical RGB color.
  /// @return STATUS_OK on success, or a negative project-defined status code on
  ///     failure.
  int set_color(const RgbColor& color) override;

  /// @brief Return the currently staged logical RGB color for this pixel.
  /// @return Current staged RGB color.
  RgbColor color() const override;

  /// @brief Return this pixel's zero-based index inside the owning strip.
  /// @return Zero-based strip index.
  std::size_t index() const override;

 private:
  /// @brief Access bridge back into the owning strip implementation.
  BoardWs2812StripAccess& strip_;
  /// @brief Zero-based pixel index owned by this view.
  std::size_t index_;
};

/// @brief Fixed-size board-owned WS2812 strip implementation.
/// @tparam LedCount Number of logical pixels owned by the strip.
template <std::size_t LedCount>
class BoardWs2812Strip final : public Ws2812Strip,
                               private BoardWs2812StripAccess {
 public:
  /// @brief Construct a strip over one OSHAL transport and wire-order policy.
  /// @param strip_name Static diagnostic name for this strip instance.
  /// @param transport OSHAL transport that flushes staged frame bytes.
  /// @param color_order Physical byte order required by the strip wiring.
  BoardWs2812Strip(const char* strip_name, oshal::Ws2812Transport& transport,
                   WireColorOrder color_order)
      : strip_name_(strip_name),
        transport_(transport),
        color_order_(color_order),
        is_initialized_(false),
        colors_{},
        frame_bytes_{},
        leds_(make_leds(std::make_index_sequence<LedCount>{})) {}

  /// @brief Return the strip's human-readable diagnostic name.
  /// @return Pointer to a static strip name.
  const char* name() const override { return strip_name_; }

  /// @brief Report whether the underlying transport backend is ready.
  /// @return True when the backend is ready.
  bool is_ready() const override { return transport_.is_ready(); }

  /// @brief Prepare the transport and push an initial cleared frame.
  /// @return STATUS_OK on success, or a negative project-defined status code on
  ///     failure.
  int initialize() override;

  /// @brief Report the number of pixels owned by this strip.
  /// @return Compile-time LED count for this strip instance.
  std::size_t led_count() const override { return LedCount; }

  /// @brief Return a mutable pixel view for the requested index.
  /// @param index Zero-based pixel index.
  /// @return Pointer to the pixel view, or nullptr when index is out of range.
  Ws2812Led* led(std::size_t index) override {
    return (index < LedCount) ? &leds_[index] : nullptr;
  }

  /// @brief Return a const pixel view for the requested index.
  /// @param index Zero-based pixel index.
  /// @return Pointer to the pixel view, or nullptr when index is out of range.
  const Ws2812Led* led(std::size_t index) const override {
    return (index < LedCount) ? &leds_[index] : nullptr;
  }

  /// @brief Stage one logical color across the whole strip.
  /// @param color Requested logical RGB color.
  /// @return STATUS_OK on success, or a negative project-defined status code on
  ///     failure.
  int fill(const RgbColor& color) override;

  /// @brief Flush the staged frame to the physical strip.
  /// @return STATUS_OK on success, or a negative project-defined status code on
  ///     failure.
  int show() override;

 private:
  template <std::size_t... Indices>
  std::array<BoardWs2812Led, LedCount> make_leds(
    std::index_sequence<Indices...>) {
    return {BoardWs2812Led(*this, Indices)...};
  }

  /// @brief Forward strip-ready checks to the public strip contract.
  /// @return True when the underlying transport is ready.
  bool strip_is_ready() const override { return is_ready(); }

  /// @brief Stage one pixel color and update the encoded frame bytes.
  /// @param index Zero-based pixel index.
  /// @param color Requested logical RGB color.
  /// @return STATUS_OK on success, or a negative project-defined status code on
  ///     failure.
  int set_pixel_color(std::size_t index, const RgbColor& color) override;

  /// @brief Return one staged pixel color.
  /// @param index Zero-based pixel index.
  /// @return The staged logical RGB color, or black when index is out of
  ///     range.
  RgbColor pixel_color(std::size_t index) const override;

  /// @brief Encode one logical RGB pixel into wire-order frame bytes.
  /// @param index Zero-based pixel index.
  /// @param color Requested logical RGB color.
  void encode_pixel(std::size_t index, const RgbColor& color);

  /// @brief Static diagnostic name reported through the BAL strip interface.
  const char* strip_name_;
  /// @brief OSHAL transport that flushes staged frame bytes to the wire.
  oshal::Ws2812Transport& transport_;
  /// @brief Physical byte ordering policy for this board's strip wiring.
  WireColorOrder color_order_;
  /// @brief True once initialize() has successfully primed the transport.
  bool is_initialized_;
  /// @brief Staged logical RGB values for each pixel.
  std::array<RgbColor, LedCount> colors_;
  /// @brief Encoded wire-order bytes written by show().
  std::array<std::uint8_t, LedCount * 3U> frame_bytes_;
  /// @brief Stable pixel-view objects handed out through led().
  std::array<BoardWs2812Led, LedCount> leds_;
};

template <std::size_t LedCount>
int BoardWs2812Strip<LedCount>::initialize() {
  int ret;

  if (is_initialized_) {
    return STATUS_OK;
  }

  if (!is_ready()) {
    return STATUS_ERR_DEVICE_UNAVAILABLE;
  }

  if (frame_bytes_.size() > transport_.max_frame_size_bytes()) {
    return STATUS_ERR_INVALID_ARGUMENT;
  }

  /* Prime the transport, then publish a cleared frame so callers start from a
   * known black-off strip state.
   */
  ret = transport_.initialize();
  if (ret < 0) {
    return ret;
  }

  ret = fill(RgbColor{0U, 0U, 0U});
  if (ret < 0) {
    return ret;
  }

  ret = transport_.write_frame(frame_bytes_.data(), frame_bytes_.size());
  if (ret < 0) {
    return ret;
  }

  is_initialized_ = true;
  return STATUS_OK;
}

template <std::size_t LedCount>
int BoardWs2812Strip<LedCount>::fill(const RgbColor& color) {
  for (std::size_t index = 0; index < LedCount; ++index) {
    const int ret = set_pixel_color(index, color);
    if (ret < 0) {
      return ret;
    }
  }

  return STATUS_OK;
}

template <std::size_t LedCount>
int BoardWs2812Strip<LedCount>::show() {
  if (!is_initialized_) {
    return STATUS_ERR_NOT_READY;
  }

  return transport_.write_frame(frame_bytes_.data(), frame_bytes_.size());
}

template <std::size_t LedCount>
int BoardWs2812Strip<LedCount>::set_pixel_color(std::size_t index,
                                                const RgbColor& color) {
  if (index >= LedCount) {
    return STATUS_ERR_INVALID_ARGUMENT;
  }

  colors_[index] = color;
  encode_pixel(index, color);
  return STATUS_OK;
}

template <std::size_t LedCount>
RgbColor BoardWs2812Strip<LedCount>::pixel_color(std::size_t index) const {
  return (index < LedCount) ? colors_[index] : RgbColor{0U, 0U, 0U};
}

template <std::size_t LedCount>
void BoardWs2812Strip<LedCount>::encode_pixel(std::size_t index,
                                              const RgbColor& color) {
  const std::size_t base_index = index * 3U;

  /* Keep logical RGB state in colors_ while emitting the board's required
   * byte order into the transport frame buffer.
   */
  switch (color_order_) {
    case WireColorOrder::kRgb:
      frame_bytes_[base_index + 0U] = color.red;
      frame_bytes_[base_index + 1U] = color.green;
      frame_bytes_[base_index + 2U] = color.blue;
      break;
    case WireColorOrder::kGrb:
      frame_bytes_[base_index + 0U] = color.green;
      frame_bytes_[base_index + 1U] = color.red;
      frame_bytes_[base_index + 2U] = color.blue;
      break;
  }
}

/// @brief Return the board-specific WS2812 strip backend selected for this
///     build.
/// @return Reference to a lazily-initialized strip backend.
Ws2812Strip& ws2812_strip_backend();

}  // namespace bal::internal

#endif /* BAL_WS2812_INTERNAL_HPP_ */