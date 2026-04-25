#ifndef OSHAL_SAMD21_WS2812_INTERNAL_HPP_
#define OSHAL_SAMD21_WS2812_INTERNAL_HPP_

#include <cstddef>
#include <cstdint>

#include "oshal/pwm.hpp"
#include "oshal/ws2812.hpp"

namespace oshal::internal {

/// @brief PWM-sequence-backed WS2812 transport for the SAMD21 PA8 path.
class Samd21PwmWs2812Transport final : public Ws2812Transport {
 public:
  /// @brief Construct a transport over a PWM output and its sequencing view.
  /// @param transport_name Static diagnostic name for the transport.
  /// @param pwm_output Physical PWM output used to drive the wire low/high.
  /// @param sequence_output Sequencing capability view for the same output.
  Samd21PwmWs2812Transport(const char* transport_name, PwmOutput& pwm_output,
                           PwmSequenceOutput& sequence_output);

  /// @brief Return a human-readable transport name.
  /// @return Pointer to a static transport name.
  const char* name() const override;

  /// @brief Report whether the underlying PWM backend is ready.
  /// @return True when the backing PWM path is ready.
  bool is_ready() const override;

  /// @brief Configure the PWM path for WS2812 bit timing.
  /// @return STATUS_OK on success, or a negative project-defined status code on
  ///     failure.
  int initialize() override;

  /// @brief Report the largest WS2812 frame that fits in the local pulse
  ///     staging buffer.
  /// @return Maximum supported frame size in bytes.
  std::size_t max_frame_size_bytes() const override;

  /// @brief Encode and transmit one WS2812 frame.
  /// @param frame_bytes Pointer to frame bytes in wire-component order.
  /// @param frame_size_bytes Number of bytes in frame_bytes.
  /// @return STATUS_OK on success, or a negative project-defined status code on
  ///     failure.
  int write_frame(const std::uint8_t* frame_bytes,
                  std::size_t frame_size_bytes) override;

 private:
  /// @brief Maximum encoded bit count staged in one local buffer.
  static constexpr std::size_t kMaxEncodedBits = 256U;

  /// @brief Expand frame bytes into per-bit pulse widths.
  /// @param frame_bytes Pointer to frame bytes in wire-component order.
  /// @param frame_size_bytes Number of bytes in frame_bytes.
  /// @param pulse_count Output pulse count produced in pulse_sequence_ns_.
  /// @return STATUS_OK on success, or a negative project-defined status code on
  ///     failure.
  int encode_frame(const std::uint8_t* frame_bytes,
                   std::size_t frame_size_bytes, std::size_t* pulse_count);

  /// @brief Static diagnostic name reported through the OSHAL transport API.
  const char* name_;
  /// @brief PWM control surface used for period configuration and idle-low
  ///     forcing.
  PwmOutput& pwm_output_;
  /// @brief Pulse-sequence view used to emit one WS2812 bitstream.
  PwmSequenceOutput& sequence_output_;
  /// @brief True once the PWM bit period has been configured.
  bool is_initialized_;
  /// @brief Per-bit high-time sequence staged for the next frame write.
  std::uint32_t pulse_sequence_ns_[kMaxEncodedBits];
};

}  // namespace oshal::internal

#endif /* OSHAL_SAMD21_WS2812_INTERNAL_HPP_ */