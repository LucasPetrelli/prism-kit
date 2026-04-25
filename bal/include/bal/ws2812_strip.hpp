#ifndef BAL_WS2812_STRIP_HPP_
#define BAL_WS2812_STRIP_HPP_

#include <cstddef>

#include "bal/rgb_led.hpp"

namespace bal {

/// @brief Logical WS2812 pixel view exposed by BAL.
class Ws2812Led : public RgbLed {
 public:
  Ws2812Led(const Ws2812Led&) = delete;
  Ws2812Led& operator=(const Ws2812Led&) = delete;
  ~Ws2812Led() override = default;

  /// @brief Return the zero-based pixel index inside the owning strip.
  /// @return Zero-based strip index.
  virtual std::size_t index() const = 0;

 protected:
  Ws2812Led() = default;
};

/// @brief Board-owned WS2812 strip interface exposed by BAL.
class Ws2812Strip {
 public:
  Ws2812Strip(const Ws2812Strip&) = delete;
  Ws2812Strip& operator=(const Ws2812Strip&) = delete;
  virtual ~Ws2812Strip() = default;

  /// @brief Return a human-readable strip name.
  /// @return Pointer to a static string describing the strip.
  virtual const char* name() const = 0;

  /// @brief Report whether the underlying transport is ready.
  /// @return True when the transport is ready, otherwise false.
  virtual bool is_ready() const = 0;

  /// @brief Prepare the strip transport and push an initial cleared frame.
  /// @return STATUS_OK on success, or a negative project-defined status code on
  ///     failure.
  virtual int initialize() = 0;

  /// @brief Report the number of addressable pixels in this strip.
  /// @return Number of WS2812 pixels owned by this strip object.
  virtual std::size_t led_count() const = 0;

  /// @brief Return a pixel view for the requested index.
  /// @param index Zero-based pixel index.
  /// @return Pointer to the requested pixel view, or nullptr when index is out
  ///     of range.
  virtual Ws2812Led* led(std::size_t index) = 0;

  /// @brief Return a const pixel view for the requested index.
  /// @param index Zero-based pixel index.
  /// @return Pointer to the requested pixel view, or nullptr when index is out
  ///     of range.
  virtual const Ws2812Led* led(std::size_t index) const = 0;

  /// @brief Fill every pixel with the same logical RGB color.
  /// @param color Requested logical RGB color.
  /// @return STATUS_OK on success, or a negative project-defined status code on
  ///     failure.
  virtual int fill(const RgbColor& color) = 0;

  /// @brief Flush the staged frame buffer to the physical strip.
  /// @return STATUS_OK on success, or a negative project-defined status code on
  ///     failure.
  virtual int show() = 0;

 protected:
  Ws2812Strip() = default;
};

/// @brief Initialize BAL-owned WS2812 strip objects.
/// @return STATUS_OK on success, or a negative project-defined status code on
///     failure.
int initialize_ws2812_strips();

/// @brief Return the board-owned WS2812 strip object.
/// @return Reference to the board-owned WS2812 strip.
Ws2812Strip& ws2812_strip();

}  // namespace bal

#endif /* BAL_WS2812_STRIP_HPP_ */