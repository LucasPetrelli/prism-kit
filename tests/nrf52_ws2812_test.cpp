#include <gtest/gtest.h>

#include <array>
#include <cerrno>
#include <cstddef>
#include <cstdint>

#include "../oshal/src/zephyr/nrf52/nrf52_ws2812_frame_internal.hpp"
#include "oshal/status.h"

namespace {

struct TestRgbPixel {
  std::uint8_t r = 0U;
  std::uint8_t g = 0U;
  std::uint8_t b = 0U;
};

using oshal::internal::nrf52::kWs2812FrameSizeBytes;
using oshal::internal::nrf52::kWs2812PixelCount;
using oshal::internal::nrf52::SubmitGrbWs2812Frame;

TEST(Nrf52Ws2812FrameTest, ConvertsSevenGrbTripletsToRgbPixels) {
  std::array<std::uint8_t, kWs2812FrameSizeBytes> frame{};
  std::array<TestRgbPixel, kWs2812PixelCount> pixels{};

  for (std::size_t index = 0U; index < kWs2812PixelCount; ++index) {
    const auto offset = index * 3U;
    frame[offset] = static_cast<std::uint8_t>(10U + index);
    frame[offset + 1U] = static_cast<std::uint8_t>(20U + index);
    frame[offset + 2U] = static_cast<std::uint8_t>(30U + index);
  }

  std::size_t submitted_pixel_count = 0U;
  const auto submit = [&submitted_pixel_count](TestRgbPixel* submitted_pixels,
                                               std::size_t count) {
    submitted_pixel_count = count;
    EXPECT_EQ(submitted_pixels[0].r, 20U);
    return 0;
  };

  EXPECT_EQ(
    SubmitGrbWs2812Frame(frame.data(), frame.size(), pixels.data(), submit),
    STATUS_OK);
  EXPECT_EQ(submitted_pixel_count, kWs2812PixelCount);
  for (std::size_t index = 0U; index < kWs2812PixelCount; ++index) {
    const auto offset = index * 3U;
    EXPECT_EQ(pixels[index].r, frame[offset + 1U]);
    EXPECT_EQ(pixels[index].g, frame[offset]);
    EXPECT_EQ(pixels[index].b, frame[offset + 2U]);
  }
}

TEST(Nrf52Ws2812FrameTest, RequiresTheCompleteSevenPixelFrame) {
  static_assert(kWs2812PixelCount == 7U);
  static_assert(kWs2812FrameSizeBytes == 21U);

  std::array<std::uint8_t, kWs2812FrameSizeBytes + 1U> frame{};
  std::array<TestRgbPixel, kWs2812PixelCount> pixels{};
  std::size_t submit_count = 0U;
  const auto submit = [&submit_count](TestRgbPixel*, std::size_t) {
    ++submit_count;
    return 0;
  };

  EXPECT_EQ(SubmitGrbWs2812Frame(frame.data(), kWs2812FrameSizeBytes,
                                 pixels.data(), submit),
            STATUS_OK);
  EXPECT_EQ(submit_count, 1U);
  EXPECT_EQ(SubmitGrbWs2812Frame(frame.data(), kWs2812FrameSizeBytes - 1U,
                                 pixels.data(), submit),
            STATUS_ERR_INVALID_ARGUMENT);
  EXPECT_EQ(
    SubmitGrbWs2812Frame(frame.data(), frame.size(), pixels.data(), submit),
    STATUS_ERR_INVALID_ARGUMENT);
  EXPECT_EQ(submit_count, 1U);
}

TEST(Nrf52Ws2812FrameTest, RejectsNullFrameAndPixelBuffer) {
  std::array<std::uint8_t, kWs2812FrameSizeBytes> frame{};
  std::array<TestRgbPixel, kWs2812PixelCount> pixels{};
  std::size_t submit_count = 0U;
  const auto submit = [&submit_count](TestRgbPixel*, std::size_t) {
    ++submit_count;
    return 0;
  };

  EXPECT_EQ(SubmitGrbWs2812Frame(nullptr, frame.size(), pixels.data(), submit),
            STATUS_ERR_INVALID_ARGUMENT);
  EXPECT_EQ(SubmitGrbWs2812Frame(frame.data(), frame.size(),
                                 static_cast<TestRgbPixel*>(nullptr), submit),
            STATUS_ERR_INVALID_ARGUMENT);
  EXPECT_EQ(SubmitGrbWs2812Frame(frame.data(), 0U, pixels.data(), submit),
            STATUS_ERR_INVALID_ARGUMENT);
  EXPECT_EQ(submit_count, 0U);
}

TEST(Nrf52Ws2812FrameTest, MapsDriverFailureToBackendStatus) {
  std::array<std::uint8_t, kWs2812FrameSizeBytes> frame{};
  std::array<TestRgbPixel, kWs2812PixelCount> pixels{};
  std::size_t submit_count = 0U;
  const auto submit = [&submit_count](TestRgbPixel*, std::size_t) {
    ++submit_count;
    return -EIO;
  };

  EXPECT_EQ(
    SubmitGrbWs2812Frame(frame.data(), frame.size(), pixels.data(), submit),
    STATUS_ERR_BACKEND);
  EXPECT_EQ(submit_count, 1U);
}

}  // namespace
