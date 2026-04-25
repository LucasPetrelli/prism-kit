#ifndef OSHAL_WS2812_HPP_
#define OSHAL_WS2812_HPP_

#include <cstddef>
#include <cstdint>

namespace oshal {

/// @brief Generic WS2812 frame output interface exposed by OSHAL.
/// @note Callers provide a full frame in WS2812 wire-component order while the
///     backend owns waveform timing, transport details, and latch handling.
class Ws2812Transport {
 public:
  Ws2812Transport(const Ws2812Transport&) = delete;
  Ws2812Transport& operator=(const Ws2812Transport&) = delete;
  virtual ~Ws2812Transport() = default;

  /// @brief Return a human-readable transport name.
  /// @return Pointer to a static string describing the transport.
  virtual const char* name() const = 0;

  /// @brief Report whether the backend for this transport is ready.
  /// @return True when the backend is ready, otherwise false.
  virtual bool is_ready() const = 0;

  /// @brief Prepare the transport for frame transmission.
  /// @return STATUS_OK on success, or a negative project-defined status code on
  ///     failure.
  virtual int initialize() = 0;

  /// @brief Report the maximum frame payload the backend can send in one call.
  /// @return Maximum supported frame size in bytes.
  virtual std::size_t max_frame_size_bytes() const = 0;

  /// @brief Transmit one WS2812 frame.
  /// @param frame_bytes Pointer to frame bytes in wire-component order.
  /// @param frame_size_bytes Number of bytes in frame_bytes.
  /// @return STATUS_OK on success, or a negative project-defined status code on
  ///     failure.
  virtual int write_frame(const std::uint8_t* frame_bytes,
                          std::size_t frame_size_bytes) = 0;

 protected:
  Ws2812Transport() = default;
};

/// @brief Public OSHAL reference to the WS2812 transport bound to SAMD21 PA8.
extern Ws2812Transport& pa8_tcc0_wo0_ws2812;

}  // namespace oshal

#endif /* OSHAL_WS2812_HPP_ */