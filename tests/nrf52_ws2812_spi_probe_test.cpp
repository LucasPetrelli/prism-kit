#include <gtest/gtest.h>

#include <array>
#include <cerrno>
#include <cstddef>
#include <cstdint>

#include "../oshal/src/zephyr/nrf52/nrf52_ws2812_spi_probe_frame_internal.hpp"
#include "oshal/status.h"

namespace {

using oshal::internal::nrf52::EncodeGrbWs2812ProbeFrame;
using oshal::internal::nrf52::kWs2812ProbeBufferBytes;
using oshal::internal::nrf52::kWs2812ProbeEncodedBytes;
using oshal::internal::nrf52::kWs2812ProbeFrameSizeBytes;
using oshal::internal::nrf52::kWs2812ProbeOneSymbol;
using oshal::internal::nrf52::kWs2812ProbePrefixBytes;
using oshal::internal::nrf52::kWs2812ProbeTailBytes;
using oshal::internal::nrf52::kWs2812ProbeZeroSymbol;
using oshal::internal::nrf52::SubmitGrbWs2812ProbeFrame;

std::uint8_t SymbolAt(const std::uint8_t* tx_bytes, std::size_t symbol_index) {
  std::uint8_t symbol = 0U;
  const std::size_t start_bit =
    kWs2812ProbePrefixBytes * 8U + symbol_index * 5U;
  for (std::size_t bit_index = 0U; bit_index < 5U; ++bit_index) {
    const std::size_t source_bit = start_bit + bit_index;
    const std::size_t source_byte = source_bit / 8U;
    const std::size_t bit_in_byte = source_bit % 8U;
    const bool bit_is_set =
      (tx_bytes[source_byte] & (0x80U >> bit_in_byte)) != 0U;
    symbol = static_cast<std::uint8_t>((symbol << 1U) | (bit_is_set ? 1U : 0U));
  }
  return symbol;
}

// Goal: preserve each input GRB byte's MSB-first data while encoding one/zero
// bits as the configured five-bit SPI symbols.
// Steps: fill a 21-byte frame with a mixed-bit pattern, submit through the
// SPI seam, then decode all 168 output symbols and check the submitted length.
TEST(Nrf52Ws2812SpiProbeTest, EncodesInputBytesMswFirstWithFiveBitSymbols) {
  std::array<std::uint8_t, kWs2812ProbeFrameSizeBytes> frame{};
  std::array<std::uint8_t, kWs2812ProbeBufferBytes> tx_bytes{};
  for (std::size_t index = 0U; index < frame.size(); ++index) {
    frame[index] = static_cast<std::uint8_t>((index * 37U) + 0x51U);
  }

  std::size_t submitted_bytes = 0U;
  const auto spi_write = [&submitted_bytes](std::uint8_t* bytes,
                                            std::size_t size) {
    EXPECT_NE(bytes, nullptr);
    submitted_bytes = size;
    return 0;
  };

  EXPECT_EQ(
    SubmitGrbWs2812ProbeFrame(frame.data(), frame.size(), tx_bytes.data(),
                              tx_bytes.size(), spi_write),
    STATUS_OK);
  EXPECT_EQ(submitted_bytes, kWs2812ProbeBufferBytes);
  EXPECT_EQ(kWs2812ProbeEncodedBytes, 105U);

  for (std::size_t symbol_index = 0U; symbol_index < frame.size() * 8U;
       ++symbol_index) {
    const std::size_t byte_index = symbol_index / 8U;
    const std::size_t input_bit = 7U - (symbol_index % 8U);
    const bool bit_is_set = (frame[byte_index] & (1U << input_bit)) != 0U;
    EXPECT_EQ(SymbolAt(tx_bytes.data(), symbol_index),
              bit_is_set ? kWs2812ProbeOneSymbol : kWs2812ProbeZeroSymbol);
  }
}

// Goal: verify the exact 0x10 zero symbol and 0x1c one symbol across a frame.
// Steps: encode seven-pixel all-zero and all-one frames and inspect all 168
// corresponding output symbols for each pattern.
TEST(Nrf52Ws2812SpiProbeTest, UsesConfiguredAllZeroAndAllOneSymbols) {
  std::array<std::uint8_t, kWs2812ProbeFrameSizeBytes> frame{};
  std::array<std::uint8_t, kWs2812ProbeBufferBytes> tx_bytes{};

  ASSERT_EQ(EncodeGrbWs2812ProbeFrame(frame.data(), frame.size(),
                                      tx_bytes.data(), tx_bytes.size()),
            STATUS_OK);
  for (std::size_t index = 0U; index < frame.size() * 8U; ++index) {
    EXPECT_EQ(SymbolAt(tx_bytes.data(), index), kWs2812ProbeZeroSymbol);
  }

  frame.fill(0xFFU);
  ASSERT_EQ(EncodeGrbWs2812ProbeFrame(frame.data(), frame.size(),
                                      tx_bytes.data(), tx_bytes.size()),
            STATUS_OK);
  for (std::size_t index = 0U; index < frame.size() * 8U; ++index) {
    EXPECT_EQ(SymbolAt(tx_bytes.data(), index), kWs2812ProbeOneSymbol);
  }
}

// Goal: keep the diagnostic transmission bounded by its low prefix and reset
// tail.
// Steps: encode a 21-byte frame and verify the prefix and all 32 tail bytes
// are zero after the 105-byte encoded payload.
TEST(Nrf52Ws2812SpiProbeTest, AddsLowPrefixAndThirtyTwoByteZeroTail) {
  std::array<std::uint8_t, kWs2812ProbeFrameSizeBytes> frame{};
  std::array<std::uint8_t, kWs2812ProbeBufferBytes> tx_bytes{};

  ASSERT_EQ(EncodeGrbWs2812ProbeFrame(frame.data(), frame.size(),
                                      tx_bytes.data(), tx_bytes.size()),
            STATUS_OK);
  EXPECT_EQ(tx_bytes[0], 0U);

  constexpr std::size_t kTailOffset =
    kWs2812ProbePrefixBytes + kWs2812ProbeEncodedBytes;
  for (std::size_t index = 0U; index < kWs2812ProbeTailBytes; ++index) {
    EXPECT_EQ(tx_bytes[kTailOffset + index], 0U);
  }
}

// Goal: reject null pointers and non-exact frame/output bounds before SPI use.
// Steps: submit null, 20-byte, 22-byte, 137-byte, and 139-byte cases and verify
// that each returns invalid-argument without invoking the SPI seam.
TEST(Nrf52Ws2812SpiProbeTest, RejectsNullAndOutOfBoundsInputsBeforeWrite) {
  std::array<std::uint8_t, kWs2812ProbeFrameSizeBytes> frame{};
  std::array<std::uint8_t, kWs2812ProbeFrameSizeBytes + 1U> oversized_frame{};
  std::array<std::uint8_t, kWs2812ProbeBufferBytes> tx_bytes{};
  std::array<std::uint8_t, kWs2812ProbeBufferBytes + 1U> oversized_tx{};
  std::size_t write_count = 0U;
  const auto spi_write = [&write_count](std::uint8_t*, std::size_t) {
    ++write_count;
    return 0;
  };

  EXPECT_EQ(SubmitGrbWs2812ProbeFrame(nullptr, frame.size(), tx_bytes.data(),
                                      tx_bytes.size(), spi_write),
            STATUS_ERR_INVALID_ARGUMENT);
  EXPECT_EQ(SubmitGrbWs2812ProbeFrame(frame.data(), frame.size(), nullptr,
                                      tx_bytes.size(), spi_write),
            STATUS_ERR_INVALID_ARGUMENT);
  EXPECT_EQ(
    SubmitGrbWs2812ProbeFrame(frame.data(), frame.size() - 1U, tx_bytes.data(),
                              tx_bytes.size(), spi_write),
    STATUS_ERR_INVALID_ARGUMENT);
  EXPECT_EQ(
    SubmitGrbWs2812ProbeFrame(oversized_frame.data(), oversized_frame.size(),
                              tx_bytes.data(), tx_bytes.size(), spi_write),
    STATUS_ERR_INVALID_ARGUMENT);
  EXPECT_EQ(
    SubmitGrbWs2812ProbeFrame(frame.data(), frame.size(), tx_bytes.data(),
                              tx_bytes.size() - 1U, spi_write),
    STATUS_ERR_INVALID_ARGUMENT);
  EXPECT_EQ(
    SubmitGrbWs2812ProbeFrame(frame.data(), frame.size(), oversized_tx.data(),
                              oversized_tx.size(), spi_write),
    STATUS_ERR_INVALID_ARGUMENT);
  EXPECT_EQ(write_count, 0U);
}

// Goal: expose a Zephyr SPI failure through the project-defined backend status.
// Steps: submit a valid 21-byte frame with an SPI seam returning -EIO and check
// that the full 138-byte buffer was submitted before STATUS_ERR_BACKEND.
TEST(Nrf52Ws2812SpiProbeTest, MapsSpiErrorsToBackendStatus) {
  std::array<std::uint8_t, kWs2812ProbeFrameSizeBytes> frame{};
  std::array<std::uint8_t, kWs2812ProbeBufferBytes> tx_bytes{};
  int submitted_size = 0;
  const auto spi_write = [&submitted_size](std::uint8_t*, std::size_t size) {
    submitted_size = static_cast<int>(size);
    return -EIO;
  };

  EXPECT_EQ(
    SubmitGrbWs2812ProbeFrame(frame.data(), frame.size(), tx_bytes.data(),
                              tx_bytes.size(), spi_write),
    STATUS_ERR_BACKEND);
  EXPECT_EQ(submitted_size, static_cast<int>(kWs2812ProbeBufferBytes));
}

}  // namespace
