#include <cstddef>
#include <cstdint>

#include "oshal/status.h"
#include "oshal/time.hpp"
#include "samd21_ws2812_internal.hpp"

namespace {

/* These timings target the standard 800 kHz WS2812 signaling window. */
constexpr std::uint32_t kWs2812BitPeriodNs = 1250U;
constexpr std::uint32_t kWs2812ZeroPulseNs = 350U;
constexpr std::uint32_t kWs2812OnePulseNs = 700U;
constexpr std::uint32_t kWs2812LatchDelayMs = 1U;

}  // namespace

namespace oshal::internal {

Samd21PwmWs2812Transport::Samd21PwmWs2812Transport(
  const char* transport_name, PwmOutput& pwm_output,
  PwmSequenceOutput& sequence_output)
    : name_(transport_name),
      pwm_output_(pwm_output),
      sequence_output_(sequence_output),
      is_initialized_(false),
      pulse_sequence_ns_{} {}

const char* Samd21PwmWs2812Transport::name() const { return name_; }

bool Samd21PwmWs2812Transport::is_ready() const {
  return pwm_output_.is_ready();
}

int Samd21PwmWs2812Transport::initialize() {
  int ret;

  if (is_initialized_) {
    return STATUS_OK;
  }

  if (!is_ready()) {
    return STATUS_ERR_DEVICE_UNAVAILABLE;
  }

  /* Keep the line driven by a constant-period PWM channel so each DMA-written
   * pulse width only needs to change the duty value for the next bit cell.
   */
  ret = pwm_output_.configure(kWs2812BitPeriodNs, 0U);
  if (ret < 0) {
    return ret;
  }

  ret = pwm_output_.enable();
  if (ret < 0) {
    return ret;
  }

  is_initialized_ = true;
  return STATUS_OK;
}

std::size_t Samd21PwmWs2812Transport::max_frame_size_bytes() const {
  const std::size_t max_sequence_bits =
    sequence_output_.max_pulse_sequence_length();
  const std::size_t bounded_bits =
    (max_sequence_bits < kMaxEncodedBits) ? max_sequence_bits : kMaxEncodedBits;
  return bounded_bits / 8U;
}

int Samd21PwmWs2812Transport::write_frame(const std::uint8_t* frame_bytes,
                                          std::size_t frame_size_bytes) {
  std::size_t pulse_count = 0U;
  int ret;

  ret = initialize();
  if (ret < 0) {
    return ret;
  }

  /* The transport expands the frame into one duty-cycle entry per wire bit,
   * lets the sequence backend shift that finite pulse train, then forces the
   * output low long enough for the WS2812 latch interval.
   */
  ret = encode_frame(frame_bytes, frame_size_bytes, &pulse_count);
  if (ret < 0) {
    return ret;
  }

  ret =
    sequence_output_.play_pulse_sequence(pulse_sequence_ns_, pulse_count, 1U);
  if (ret < 0) {
    return ret;
  }

  while (sequence_output_.is_pulse_sequence_active()) {
    oshal::sleep_ms(1U);
  }

  ret = pwm_output_.set_pulse(0U);
  if (ret < 0) {
    return ret;
  }

  oshal::sleep_ms(kWs2812LatchDelayMs);
  return STATUS_OK;
}

int Samd21PwmWs2812Transport::encode_frame(const std::uint8_t* frame_bytes,
                                           std::size_t frame_size_bytes,
                                           std::size_t* pulse_count) {
  std::size_t encoded_bit_count = 0U;

  if ((frame_bytes == nullptr) || (pulse_count == nullptr) ||
      (frame_size_bytes == 0U)) {
    return STATUS_ERR_INVALID_ARGUMENT;
  }

  if (frame_size_bytes > max_frame_size_bytes()) {
    return STATUS_ERR_INVALID_ARGUMENT;
  }

  for (std::size_t byte_index = 0; byte_index < frame_size_bytes;
       ++byte_index) {
    const std::uint8_t current_byte = frame_bytes[byte_index];

    /* WS2812 consumes each byte MSB-first, with duty width selecting logical
     * zero versus one for a fixed 1.25 us bit period.
     */
    for (std::uint8_t bit_index = 0U; bit_index < 8U; ++bit_index) {
      const std::uint8_t bit_mask =
        static_cast<std::uint8_t>(0x80U >> bit_index);
      pulse_sequence_ns_[encoded_bit_count] = ((current_byte & bit_mask) != 0U)
                                                ? kWs2812OnePulseNs
                                                : kWs2812ZeroPulseNs;
      ++encoded_bit_count;
    }
  }

  *pulse_count = encoded_bit_count;
  return STATUS_OK;
}

}  // namespace oshal::internal