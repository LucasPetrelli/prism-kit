#include <gtest/gtest.h>

#include <array>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <limits>

#include "../oshal/src/zephyr/nrf52/nrf52_ws2812_frame_internal.hpp"
#include "oshal/status.h"

namespace {

using oshal::internal::nrf52::CalculateWs2812EncodedBytes;
using oshal::internal::nrf52::CalculateWs2812ResetBytes;
using oshal::internal::nrf52::CalculateWs2812TransferBytes;
using oshal::internal::nrf52::EncodeFramedGrbWs2812Frame;
using oshal::internal::nrf52::kWs2812FrameSizeBytes;
using oshal::internal::nrf52::kWs2812PixelCount;
using oshal::internal::nrf52::SubmitFramedGrbWs2812Frame;
using oshal::internal::nrf52::Ws2812SymbolFits;

constexpr std::size_t kResetBytes = 500U;
constexpr std::size_t kBitsPerSymbol = 5U;
constexpr std::uint8_t kZeroSymbol = 0x08U;
constexpr std::uint8_t kOneSymbol = 0x0EU;
constexpr std::size_t kEncodedBytes = 105U;
constexpr std::size_t kTransferBytes = 1105U;

using TestFrame = std::array<std::uint8_t, kWs2812FrameSizeBytes>;
using TestTransfer = std::array<std::uint8_t, kTransferBytes>;

void ExpectEncodedPayload(const TestTransfer& transfer,
                          const TestFrame& expected_frame) {
  const std::size_t payload_offset = kResetBytes;
  std::size_t output_bit_offset = payload_offset * 8U;

  for (const auto input_byte : expected_frame) {
    for (int input_bit = 7; input_bit >= 0; --input_bit) {
      std::uint8_t actual_symbol = 0U;
      for (std::size_t symbol_bit = 0U; symbol_bit < kBitsPerSymbol;
           ++symbol_bit) {
        const std::size_t byte_index = output_bit_offset / 8U;
        const std::uint8_t bit_mask =
          static_cast<std::uint8_t>(0x80U >> (output_bit_offset % 8U));
        actual_symbol = static_cast<std::uint8_t>(
          (actual_symbol << 1U) |
          ((transfer[byte_index] & bit_mask) != 0U ? 1U : 0U));
        ++output_bit_offset;
      }

      const bool input_is_one = (input_byte & (1U << input_bit)) != 0U;
      EXPECT_EQ(actual_symbol, input_is_one ? kOneSymbol : kZeroSymbol);
    }
  }

  for (std::size_t index = 0U; index < kResetBytes; ++index) {
    EXPECT_EQ(transfer[index], 0U);
    EXPECT_EQ(transfer[kResetBytes + kEncodedBytes + index], 0U);
  }
}

TestFrame MakePatternFrame(std::uint8_t start) {
  TestFrame frame{};
  for (std::size_t index = 0U; index < frame.size(); ++index) {
    frame[index] = static_cast<std::uint8_t>(start + index * 13U);
  }
  return frame;
}

TEST(Nrf52Ws2812FrameTest, DerivesResetAndPayloadLengthsFromTiming) {
  EXPECT_EQ(CalculateWs2812ResetBytes(4000000U, 1000U), kResetBytes);
  EXPECT_EQ(CalculateWs2812ResetBytes(4000000U, 1U), 1U);
  EXPECT_EQ(CalculateWs2812ResetBytes(1000000U, 1000U), 125U);
  EXPECT_EQ(CalculateWs2812ResetBytes(0U, 1000U), 0U);
  EXPECT_EQ(CalculateWs2812ResetBytes(4000000U, 0U), 0U);
  EXPECT_EQ(
    CalculateWs2812ResetBytes(std::numeric_limits<std::uint64_t>::max(), 2U),
    0U);
  EXPECT_EQ(CalculateWs2812EncodedBytes(kBitsPerSymbol), kEncodedBytes);
  EXPECT_EQ(CalculateWs2812EncodedBytes(0U), 0U);
  EXPECT_EQ(CalculateWs2812EncodedBytes(9U), 0U);
  EXPECT_EQ(CalculateWs2812TransferBytes(kResetBytes, kBitsPerSymbol),
            kTransferBytes);
  EXPECT_EQ(CalculateWs2812TransferBytes(
              std::numeric_limits<std::size_t>::max(), kBitsPerSymbol),
            0U);
}

// Goal: prove each invocation emits one contiguous reset/data/reset transfer.
// Steps: submit distinct GRB frames twice and decode every five-bit symbol,
// checking the full transfer length and both literal-zero reset regions.
TEST(Nrf52Ws2812FrameTest, FramesEverySubmittedFrameInOneTransfer) {
  const TestFrame first_frame = MakePatternFrame(3U);
  const TestFrame second_frame = MakePatternFrame(91U);
  TestTransfer transfer{};
  std::size_t submit_count = 0U;

  const auto submit = [&](std::uint8_t* bytes, std::size_t size) {
    ++submit_count;
    EXPECT_EQ(bytes, transfer.data());
    EXPECT_EQ(size, kTransferBytes);
    if (submit_count == 1U) {
      ExpectEncodedPayload(transfer, first_frame);
    } else {
      ExpectEncodedPayload(transfer, second_frame);
    }
    return 0;
  };

  EXPECT_EQ(
    SubmitFramedGrbWs2812Frame(first_frame.data(), first_frame.size(),
                               transfer.data(), transfer.size(), kResetBytes,
                               kBitsPerSymbol, kZeroSymbol, kOneSymbol, submit),
    STATUS_OK);
  EXPECT_EQ(submit_count, 1U);

  EXPECT_EQ(
    SubmitFramedGrbWs2812Frame(second_frame.data(), second_frame.size(),
                               transfer.data(), transfer.size(), kResetBytes,
                               kBitsPerSymbol, kZeroSymbol, kOneSymbol, submit),
    STATUS_OK);
  EXPECT_EQ(submit_count, 2U);
}

TEST(Nrf52Ws2812FrameTest, EncodesSevenPixelGrbFrameAtTheConfiguredSymbols) {
  static_assert(kWs2812PixelCount == 7U);
  static_assert(kWs2812FrameSizeBytes == 21U);
  static_assert(kTransferBytes == 1105U);

  const TestFrame frame = MakePatternFrame(0U);
  TestTransfer transfer{};
  EXPECT_EQ(EncodeFramedGrbWs2812Frame(
              frame.data(), frame.size(), transfer.data(), transfer.size(),
              kResetBytes, kBitsPerSymbol, kZeroSymbol, kOneSymbol),
            STATUS_OK);
  ExpectEncodedPayload(transfer, frame);
}

// Goal: reject malformed frames and unsupported symbol settings before I/O.
// Steps: exercise null/incorrect sizes, zero reset length, bad symbol widths,
// and symbols that do not fit; verify the submit callback is never called.
TEST(Nrf52Ws2812FrameTest, InvalidFrameDoesNotSubmit) {
  const TestFrame frame = MakePatternFrame(1U);
  std::array<std::uint8_t, kTransferBytes - 1U> short_transfer{};
  TestTransfer transfer{};
  std::size_t submit_count = 0U;
  const auto submit = [&submit_count](std::uint8_t*, std::size_t) {
    ++submit_count;
    return 0;
  };

  EXPECT_EQ(SubmitFramedGrbWs2812Frame(
              nullptr, frame.size(), transfer.data(), transfer.size(),
              kResetBytes, kBitsPerSymbol, kZeroSymbol, kOneSymbol, submit),
            STATUS_ERR_INVALID_ARGUMENT);
  EXPECT_EQ(SubmitFramedGrbWs2812Frame(
              frame.data(), frame.size(), static_cast<std::uint8_t*>(nullptr),
              transfer.size(), kResetBytes, kBitsPerSymbol, kZeroSymbol,
              kOneSymbol, submit),
            STATUS_ERR_INVALID_ARGUMENT);
  EXPECT_EQ(SubmitFramedGrbWs2812Frame(
              frame.data(), frame.size() - 1U, transfer.data(), transfer.size(),
              kResetBytes, kBitsPerSymbol, kZeroSymbol, kOneSymbol, submit),
            STATUS_ERR_INVALID_ARGUMENT);
  EXPECT_EQ(SubmitFramedGrbWs2812Frame(
              frame.data(), frame.size() + 1U, transfer.data(), transfer.size(),
              kResetBytes, kBitsPerSymbol, kZeroSymbol, kOneSymbol, submit),
            STATUS_ERR_INVALID_ARGUMENT);
  EXPECT_EQ(SubmitFramedGrbWs2812Frame(
              frame.data(), frame.size(), transfer.data(), transfer.size() - 1U,
              kResetBytes, kBitsPerSymbol, kZeroSymbol, kOneSymbol, submit),
            STATUS_ERR_INVALID_ARGUMENT);
  EXPECT_EQ(
    SubmitFramedGrbWs2812Frame(
      frame.data(), frame.size(), short_transfer.data(), short_transfer.size(),
      kResetBytes, kBitsPerSymbol, kZeroSymbol, kOneSymbol, submit),
    STATUS_ERR_INVALID_ARGUMENT);
  EXPECT_EQ(SubmitFramedGrbWs2812Frame(
              frame.data(), frame.size(), transfer.data(), transfer.size(), 0U,
              kBitsPerSymbol, kZeroSymbol, kOneSymbol, submit),
            STATUS_ERR_INVALID_ARGUMENT);
  EXPECT_EQ(SubmitFramedGrbWs2812Frame(
              frame.data(), frame.size(), transfer.data(), transfer.size(),
              kResetBytes, 2U, kZeroSymbol, kOneSymbol, submit),
            STATUS_ERR_INVALID_ARGUMENT);
  EXPECT_EQ(SubmitFramedGrbWs2812Frame(
              frame.data(), frame.size(), transfer.data(), transfer.size(),
              kResetBytes, kBitsPerSymbol, 0x20U, kOneSymbol, submit),
            STATUS_ERR_INVALID_ARGUMENT);
  EXPECT_EQ(submit_count, 0U);
}

TEST(Nrf52Ws2812FrameTest, RejectsSymbolsOutsideTheConfiguredWidth) {
  EXPECT_TRUE(Ws2812SymbolFits(kZeroSymbol, kBitsPerSymbol));
  EXPECT_TRUE(Ws2812SymbolFits(kOneSymbol, kBitsPerSymbol));
  EXPECT_FALSE(Ws2812SymbolFits(0x20U, kBitsPerSymbol));
  EXPECT_FALSE(Ws2812SymbolFits(kZeroSymbol, 0U));
  EXPECT_FALSE(Ws2812SymbolFits(kZeroSymbol, 9U));
}

TEST(Nrf52Ws2812FrameTest, MapsSpiFailureToBackendStatus) {
  const TestFrame frame = MakePatternFrame(7U);
  TestTransfer transfer{};
  std::size_t submit_count = 0U;
  const auto submit = [&submit_count](std::uint8_t*, std::size_t) {
    ++submit_count;
    return -EIO;
  };

  EXPECT_EQ(SubmitFramedGrbWs2812Frame(
              frame.data(), frame.size(), transfer.data(), transfer.size(),
              kResetBytes, kBitsPerSymbol, kZeroSymbol, kOneSymbol, submit),
            STATUS_ERR_BACKEND);
  EXPECT_EQ(submit_count, 1U);
}

}  // namespace
