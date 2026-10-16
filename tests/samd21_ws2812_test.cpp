#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

#include "../oshal/src/samd21/samd21_ws2812_internal.hpp"
#include "debug_port_test_access.hpp"
#include "gtest/gtest.h"
#include "oshal/status.h"
#include "oshal/time.h"

namespace {

constexpr std::size_t kMaxPulseSequenceEntries = 281U;
constexpr std::size_t kWs2812PreamblePulseCount = 24U;
constexpr std::size_t kWs2812DataPulseCount = 21U * 8U;
constexpr std::uint32_t kWs2812ZeroPulseNs = 350U;
constexpr std::uint32_t kWs2812OnePulseNs = 700U;

std::array<std::uint32_t, 4U> g_sleep_durations{};
std::size_t g_sleep_call_count = 0U;

class FakePwmOutput final : public oshal::PwmOutput {
 public:
  const char* Name() const override { return "fake-pwm"; }
  bool IsReady() const override { return true; }

  int Configure(std::uint32_t period_ns, std::uint32_t pulse_ns) override {
    ++configure_call_count_;
    configured_period_ns_ = period_ns;
    configured_pulse_ns_ = pulse_ns;
    return STATUS_OK;
  }

  int SetPulse(std::uint32_t pulse_ns) override {
    ++set_pulse_call_count_;
    last_pulse_ns_ = pulse_ns;
    return STATUS_OK;
  }

  int Enable() override {
    ++enable_call_count_;
    return STATUS_OK;
  }

  int Disable() override { return STATUS_OK; }

  std::size_t ConfigureCallCount() const { return configure_call_count_; }
  std::uint32_t ConfiguredPeriodNs() const { return configured_period_ns_; }
  std::uint32_t ConfiguredPulseNs() const { return configured_pulse_ns_; }
  std::size_t EnableCallCount() const { return enable_call_count_; }
  std::size_t SetPulseCallCount() const { return set_pulse_call_count_; }
  std::uint32_t LastPulseNs() const { return last_pulse_ns_; }

 private:
  std::size_t configure_call_count_ = 0U;
  std::uint32_t configured_period_ns_ = 0U;
  std::uint32_t configured_pulse_ns_ = 0U;
  std::size_t enable_call_count_ = 0U;
  std::size_t set_pulse_call_count_ = 0U;
  std::uint32_t last_pulse_ns_ = 0U;
};

class FakePulseSequenceOutput final : public oshal::PwmSequenceOutput {
 public:
  int PlayPulseSequence(const std::uint32_t* pulse_ns_sequence,
                        std::size_t pulse_count,
                        std::uint32_t repeat_count) override {
    if ((pulse_ns_sequence == nullptr) ||
        (pulse_count > max_pulse_sequence_length_) ||
        (pulse_count > pulse_ns_sequence_.size())) {
      return STATUS_ERR_INVALID_ARGUMENT;
    }

    std::copy_n(pulse_ns_sequence, pulse_count, pulse_ns_sequence_.begin());
    ++play_call_count_;
    pulse_count_ = pulse_count;
    repeat_count_ = repeat_count;
    return STATUS_OK;
  }

  int StopPulseSequence() override { return STATUS_OK; }
  bool IsPulseSequenceActive() const override { return false; }
  std::size_t MaxPulseSequenceLength() const override {
    return max_pulse_sequence_length_;
  }

  void SetMaxPulseSequenceLength(std::size_t max_pulse_sequence_length) {
    max_pulse_sequence_length_ = max_pulse_sequence_length;
  }

  std::size_t PlayCallCount() const { return play_call_count_; }
  std::size_t PulseCount() const { return pulse_count_; }
  std::uint32_t RepeatCount() const { return repeat_count_; }
  const std::array<std::uint32_t, kMaxPulseSequenceEntries>& PulseSequenceNs()
    const {
    return pulse_ns_sequence_;
  }

 private:
  std::array<std::uint32_t, kMaxPulseSequenceEntries> pulse_ns_sequence_{};
  std::size_t max_pulse_sequence_length_ = kMaxPulseSequenceEntries;
  std::size_t play_call_count_ = 0U;
  std::size_t pulse_count_ = 0U;
  std::uint32_t repeat_count_ = 0U;
};

class Samd21Ws2812TransportTest : public ::testing::Test {
 protected:
  Samd21Ws2812TransportTest()
      : transport_("test-ws2812", pwm_output_, sequence_output_) {}

  void SetUp() override {
    g_sleep_durations.fill(0U);
    g_sleep_call_count = 0U;
  }

  FakePwmOutput pwm_output_;
  FakePulseSequenceOutput sequence_output_;
  oshal::internal::Samd21PwmWs2812Transport transport_;
};

// Goal: prepend 24 zero-duty entries without changing the 168 payload pulses,
// then schedule exactly one idle-low tail.
// Steps: Submit a mixed 21-byte frame, verify all 24 preamble values and all
// encoded data widths, then check the zero tail, finite repeat, foreground
// low write, and existing 1 ms latch sleep.
TEST_F(Samd21Ws2812TransportTest,
       TwentyOneByteFrameKeepsDataAndAddsOneZeroTail) {
  constexpr std::array<std::uint8_t, 21U> frame{
    0x00U, 0xFFU, 0xA5U, 0x5AU, 0x80U, 0x01U, 0xC3U, 0x3CU, 0x96U, 0x69U, 0xF0U,
    0x0FU, 0xAAU, 0x55U, 0x7EU, 0x81U, 0x18U, 0xE7U, 0x42U, 0x24U, 0xBDU,
  };

  ASSERT_EQ(transport_.MaxFrameSizeBytes(), 32U);
  ASSERT_EQ(transport_.WriteFrame(frame.data(), frame.size()), STATUS_OK);

  ASSERT_EQ(sequence_output_.PlayCallCount(), 1U);
  ASSERT_EQ(sequence_output_.PulseCount(),
            kWs2812PreamblePulseCount + kWs2812DataPulseCount + 1U);
  EXPECT_EQ(sequence_output_.RepeatCount(), 1U);

  for (std::size_t pulse_index = 0U; pulse_index < kWs2812PreamblePulseCount;
       ++pulse_index) {
    EXPECT_EQ(sequence_output_.PulseSequenceNs()[pulse_index], 0U)
      << "preamble entry " << pulse_index;
  }

  std::size_t pulse_index = kWs2812PreamblePulseCount;
  for (const std::uint8_t current_byte : frame) {
    for (std::uint8_t bit_index = 0U; bit_index < 8U; ++bit_index) {
      const std::uint8_t bit_mask =
        static_cast<std::uint8_t>(0x80U >> bit_index);
      const std::uint32_t expected_pulse_ns = ((current_byte & bit_mask) != 0U)
                                                ? kWs2812OnePulseNs
                                                : kWs2812ZeroPulseNs;
      EXPECT_EQ(sequence_output_.PulseSequenceNs()[pulse_index],
                expected_pulse_ns)
        << "data pulse index " << pulse_index;
      ++pulse_index;
    }
  }

  ASSERT_EQ(pulse_index, kWs2812PreamblePulseCount + kWs2812DataPulseCount);
  EXPECT_EQ(sequence_output_.PulseSequenceNs()[pulse_index], 0U);
  EXPECT_EQ(pwm_output_.ConfigureCallCount(), 1U);
  EXPECT_EQ(pwm_output_.ConfiguredPeriodNs(), 1250U);
  EXPECT_EQ(pwm_output_.ConfiguredPulseNs(), 0U);
  EXPECT_EQ(pwm_output_.EnableCallCount(), 1U);
  EXPECT_EQ(pwm_output_.SetPulseCallCount(), 1U);
  EXPECT_EQ(pwm_output_.LastPulseNs(), 0U);
  ASSERT_EQ(g_sleep_call_count, 1U);
  EXPECT_EQ(g_sleep_durations[0U], 1U);
}

// Goal: keep the advertised 32-byte limit when preamble and tail entries use
// sequence slots.
// Steps: Submit 32 bytes of one-bits and verify 24 zero preamble entries,
// 256 unchanged data widths, one zero tail, and one finite repeat fit in 281
// entries.
TEST_F(Samd21Ws2812TransportTest, ThirtyTwoByteFrameFitsThe281EntrySequence) {
  std::array<std::uint8_t, 32U> frame{};
  frame.fill(0xFFU);

  ASSERT_EQ(transport_.MaxFrameSizeBytes(), 32U);
  ASSERT_EQ(transport_.WriteFrame(frame.data(), frame.size()), STATUS_OK);
  ASSERT_EQ(sequence_output_.PulseCount(), kMaxPulseSequenceEntries);
  EXPECT_EQ(sequence_output_.RepeatCount(), 1U);
  for (std::size_t pulse_index = 0U; pulse_index < kWs2812PreamblePulseCount;
       ++pulse_index) {
    EXPECT_EQ(sequence_output_.PulseSequenceNs()[pulse_index], 0U)
      << "preamble entry " << pulse_index;
  }
  for (std::size_t pulse_index = kWs2812PreamblePulseCount;
       pulse_index < kWs2812PreamblePulseCount + 256U; ++pulse_index) {
    EXPECT_EQ(sequence_output_.PulseSequenceNs()[pulse_index],
              kWs2812OnePulseNs)
      << "data pulse index " << pulse_index;
  }
  EXPECT_EQ(sequence_output_.PulseSequenceNs()[280U], 0U);
}

// Goal: reserve preamble and tail entries when the backend capacity is below
// the transport's local maximum.
// Steps: Limit the fake backend to 280 entries, verify the 31-byte maximum and
// a complete 31-byte sequence, then confirm a 32-byte frame is rejected.
TEST_F(Samd21Ws2812TransportTest,
       SmallerBackendCapacityReservesPreambleAndTail) {
  sequence_output_.SetMaxPulseSequenceLength(280U);
  std::array<std::uint8_t, 31U> frame{};
  frame.fill(0xFFU);
  constexpr std::array<std::uint8_t, 32U> oversized_frame{};

  EXPECT_EQ(transport_.MaxFrameSizeBytes(), 31U);
  ASSERT_EQ(transport_.WriteFrame(frame.data(), frame.size()), STATUS_OK);
  ASSERT_EQ(sequence_output_.PulseCount(),
            kWs2812PreamblePulseCount + (31U * 8U) + 1U);
  for (std::size_t pulse_index = 0U; pulse_index < kWs2812PreamblePulseCount;
       ++pulse_index) {
    EXPECT_EQ(sequence_output_.PulseSequenceNs()[pulse_index], 0U)
      << "preamble entry " << pulse_index;
  }
  EXPECT_EQ(sequence_output_.PulseSequenceNs()[272U], 0U);
  EXPECT_EQ(
    transport_.WriteFrame(oversized_frame.data(), oversized_frame.size()),
    STATUS_ERR_INVALID_ARGUMENT);
  EXPECT_EQ(sequence_output_.PlayCallCount(), 1U);
}

// Goal: report no payload capacity when preamble and tail consume every
// sequence entry.
// Steps: Set the backend capacity to exactly 25 entries and verify a one-byte
// frame is rejected without starting DMA playback.
TEST_F(Samd21Ws2812TransportTest,
       BackendCapacityUsedOnlyByPreambleAndTailHasNoPayloadRoom) {
  sequence_output_.SetMaxPulseSequenceLength(25U);
  constexpr std::array<std::uint8_t, 1U> frame{};

  EXPECT_EQ(transport_.MaxFrameSizeBytes(), 0U);
  EXPECT_EQ(transport_.WriteFrame(frame.data(), frame.size()),
            STATUS_ERR_INVALID_ARGUMENT);
  EXPECT_EQ(sequence_output_.PlayCallCount(), 0U);
  EXPECT_EQ(pwm_output_.SetPulseCallCount(), 0U);
  EXPECT_EQ(g_sleep_call_count, 0U);
}

// Goal: reject a payload larger than the advertised transport maximum.
// Steps: Submit 33 bytes to a fresh transport and verify no sequence starts and
// no foreground latch actions are reported for the rejected frame.
TEST_F(Samd21Ws2812TransportTest, ThirtyThreeByteFrameIsRejected) {
  constexpr std::array<std::uint8_t, 33U> oversized_frame{};

  EXPECT_EQ(transport_.MaxFrameSizeBytes(), 32U);
  EXPECT_EQ(
    transport_.WriteFrame(oversized_frame.data(), oversized_frame.size()),
    STATUS_ERR_INVALID_ARGUMENT);
  EXPECT_EQ(sequence_output_.PlayCallCount(), 0U);
  EXPECT_EQ(pwm_output_.SetPulseCallCount(), 0U);
  EXPECT_EQ(g_sleep_call_count, 0U);
}

// Goal: log the accepted frame bytes and the resulting DMA pulse sequence.
// Steps: submit a mixed three-byte frame and verify the original bytes precede
// the complete decimal sequence, comma-delimited on one line.
TEST_F(Samd21Ws2812TransportTest, LogsFrameBytesAndPulseSequence) {
  constexpr std::array<std::uint8_t, 3U> frame{0x00U, 0xA5U, 0xFFU};
  constexpr std::array<std::uint32_t, 24U> expected_payload_pulses{
    350U, 350U, 350U, 350U, 350U, 350U, 350U, 350U, 700U, 350U, 700U, 350U,
    350U, 700U, 350U, 700U, 700U, 700U, 700U, 700U, 700U, 700U, 700U, 700U,
  };

  prism::test::ClearDebugPortOutput();
  ASSERT_EQ(transport_.WriteFrame(frame.data(), frame.size()), STATUS_OK);

  std::string expected_output =
    "[OSHAL][WS2812] DMA frame (3 bytes): 00 A5 FF\n"
    "[OSHAL][WS2812] DMA pulse sequence ns: ";
  for (std::size_t pulse_index = 0U; pulse_index < kWs2812PreamblePulseCount;
       ++pulse_index) {
    if (pulse_index > 0U) {
      expected_output += ',';
    }
    expected_output += '0';
  }
  for (const std::uint32_t pulse_ns : expected_payload_pulses) {
    expected_output += ',';
    expected_output += std::to_string(pulse_ns);
  }
  expected_output += ",0\n";

  EXPECT_EQ(prism::test::DebugPortOutput(), expected_output);
}

}  // namespace

extern "C" void oshal_sleep_ms(uint32_t duration_ms) {
  if (g_sleep_call_count < g_sleep_durations.size()) {
    g_sleep_durations[g_sleep_call_count] = duration_ms;
  }
  ++g_sleep_call_count;
}
