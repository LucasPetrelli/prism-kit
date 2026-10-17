/// @file Reference-vector tests for the private APP OKLab color helper.

#include "oklab.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string>

#include "gtest/gtest.h"
#include "prism/color.hpp"

namespace {

using prism::color::RgbColor;
using prism::controller_detail::InterpolateRgb;

struct ReferenceLab {
  double l;
  double a;
  double b;
};

struct ReferenceSample {
  RgbColor rgb;
  bool clipped;
};

double DecodeSrgb(std::uint8_t channel) {
  const double value = static_cast<double>(channel) / 255.0;
  return (value <= 0.04045) ? value / 12.92
                            : std::pow((value + 0.055) / 1.055, 2.4);
}

std::uint8_t EncodeSrgb(double linear) {
  const double encoded = (linear <= 0.0031308)
                           ? 12.92 * linear
                           : 1.055 * std::pow(linear, 1.0 / 2.4) - 0.055;
  return static_cast<std::uint8_t>(std::lround(encoded * 255.0));
}

ReferenceLab ToReferenceLab(RgbColor rgb) {
  const double r = DecodeSrgb(rgb.red);
  const double g = DecodeSrgb(rgb.green);
  const double b = DecodeSrgb(rgb.blue);
  const double l =
    std::cbrt(0.4122214708 * r + 0.5363325363 * g + 0.0514459929 * b);
  const double m =
    std::cbrt(0.2119034982 * r + 0.6806995451 * g + 0.1073969566 * b);
  const double s =
    std::cbrt(0.0883024619 * r + 0.2817188376 * g + 0.6299787005 * b);
  return ReferenceLab{0.2104542553 * l + 0.7936177850 * m - 0.0040720468 * s,
                      1.9779984951 * l - 2.4285922050 * m + 0.4505937099 * s,
                      0.0259040371 * l + 0.7827717662 * m - 0.8086757660 * s};
}

ReferenceSample FromReferenceLab(ReferenceLab lab) {
  const double l_root = lab.l + 0.3963377774 * lab.a + 0.2158037573 * lab.b;
  const double m_root = lab.l - 0.1055613458 * lab.a - 0.0638541728 * lab.b;
  const double s_root = lab.l - 0.0894841775 * lab.a - 1.2914855480 * lab.b;
  const double l = l_root * l_root * l_root;
  const double m = m_root * m_root * m_root;
  const double s = s_root * s_root * s_root;
  const double red = 4.0767416621 * l - 3.3077115913 * m + 0.2309699292 * s;
  const double green = -1.2684380046 * l + 2.6097574011 * m - 0.3413193965 * s;
  const double blue = -0.0041960863 * l - 0.7034186147 * m + 1.7076147010 * s;
  const bool clipped = red < 0.0 || red > 1.0 || green < 0.0 || green > 1.0 ||
                       blue < 0.0 || blue > 1.0;
  return ReferenceSample{
    RgbColor{EncodeSrgb(std::clamp(red, 0.0, 1.0)),
             EncodeSrgb(std::clamp(green, 0.0, 1.0)),
             EncodeSrgb(std::clamp(blue, 0.0, 1.0))},
    clipped,
  };
}

ReferenceSample ReferenceInterpolate(RgbColor source, RgbColor target,
                                     std::uint16_t elapsed,
                                     std::uint16_t duration) {
  if (duration == 0U || elapsed >= duration) {
    return ReferenceSample{target, false};
  }
  if (elapsed == 0U) {
    return ReferenceSample{source, false};
  }
  const ReferenceLab start = ToReferenceLab(source);
  const ReferenceLab end = ToReferenceLab(target);
  const double progress = static_cast<double>(elapsed) / duration;
  const ReferenceLab sample{
    start.l + (end.l - start.l) * progress,
    start.a + (end.a - start.a) * progress,
    start.b + (end.b - start.b) * progress,
  };
  return FromReferenceLab(sample);
}

int ChannelError(std::uint8_t actual, std::uint8_t expected) {
  return std::abs(static_cast<int>(actual) - static_cast<int>(expected));
}

TEST(OklabTest, ReferenceVectorsStayWithinOneSrgbCodeValue) {
  struct Path {
    const char* name;
    RgbColor source;
    RgbColor target;
  };
  constexpr std::array<Path, 10U> paths{{
    {"black-white", {0U, 0U, 0U}, {255U, 255U, 255U}},
    {"white-black", {255U, 255U, 255U}, {0U, 0U, 0U}},
    {"red-blue", {255U, 0U, 0U}, {0U, 0U, 255U}},
    {"blue-green", {0U, 0U, 255U}, {0U, 255U, 0U}},
    {"green-red", {0U, 255U, 0U}, {255U, 0U, 0U}},
    {"yellow-cyan", {255U, 255U, 0U}, {0U, 255U, 255U}},
    {"magenta-green", {255U, 0U, 255U}, {0U, 255U, 0U}},
    {"near-gray", {100U, 101U, 102U}, {130U, 129U, 128U}},
    {"mixed-blue-orange", {17U, 83U, 201U}, {230U, 44U, 77U}},
    {"mixed-warm-cyan", {250U, 120U, 5U}, {8U, 210U, 184U}},
  }};
  constexpr std::array<std::uint16_t, 5U> elapsed_values{0U, 1U, 2U, 3U, 4U};
  bool saw_clipped_intermediate = false;

  for (const Path& path : paths) {
    for (const std::uint16_t elapsed : elapsed_values) {
      const RgbColor actual =
        InterpolateRgb(path.source, path.target, elapsed, 4U);
      const ReferenceSample expected =
        ReferenceInterpolate(path.source, path.target, elapsed, 4U);
      saw_clipped_intermediate = saw_clipped_intermediate || expected.clipped;
      const std::string label =
        std::string(path.name) + " at " + std::to_string(elapsed * 25U) + "%";
      EXPECT_LE(ChannelError(actual.red, expected.rgb.red), 1)
        << label << " red actual=" << static_cast<int>(actual.red)
        << " expected=" << static_cast<int>(expected.rgb.red);
      EXPECT_LE(ChannelError(actual.green, expected.rgb.green), 1)
        << label << " green actual=" << static_cast<int>(actual.green)
        << " expected=" << static_cast<int>(expected.rgb.green);
      EXPECT_LE(ChannelError(actual.blue, expected.rgb.blue), 1)
        << label << " blue actual=" << static_cast<int>(actual.blue)
        << " expected=" << static_cast<int>(expected.rgb.blue);
      if (elapsed == 0U) {
        EXPECT_EQ(actual, path.source) << label << " exact source endpoint";
      } else if (elapsed == 4U) {
        EXPECT_EQ(actual, path.target) << label << " exact target endpoint";
      }
    }
  }

  EXPECT_TRUE(saw_clipped_intermediate)
    << "The reference matrix must exercise out-of-gamut linear clipping.";
}

TEST(OklabTest, NeutralGrayRoundTripsAcrossTheSrgbByteRange) {
  for (std::uint16_t channel = 0U; channel <= 255U; ++channel) {
    const auto value = static_cast<std::uint8_t>(channel);
    const RgbColor gray{value, value, value};
    const auto lab = prism::controller_detail::ToOklab(gray);
    const RgbColor actual = InterpolateRgb(gray, gray, 1U, 2U);
    EXPECT_LE(ChannelError(actual.red, value), 1)
      << "gray " << channel << " actual=" << static_cast<int>(actual.red)
      << " lab=" << lab.l << ',' << lab.a << ',' << lab.b;
    EXPECT_LE(ChannelError(actual.green, value), 1)
      << "gray " << channel << " actual=" << static_cast<int>(actual.green);
    EXPECT_LE(ChannelError(actual.blue, value), 1)
      << "gray " << channel << " actual=" << static_cast<int>(actual.blue);
  }
}

TEST(OklabTest,
     InterpolationRoundsDeterministicallyAndHandlesFullDurationRange) {
  constexpr RgbColor source{255U, 0U, 17U};
  constexpr RgbColor target{0U, 255U, 231U};
  const RgbColor first = InterpolateRgb(source, target, 32767U, 65535U);
  const RgbColor second = InterpolateRgb(source, target, 32767U, 65535U);

  EXPECT_EQ(first, second);
  EXPECT_EQ(InterpolateRgb(source, target, 0U, 0U), target);
  EXPECT_EQ(InterpolateRgb(source, target, 65535U, 65535U), target);
}

TEST(OklabTest, FixedPointCoordinatesStayInsideConservativeMatrixBounds) {
  // For Q2.13 LMS roots in [0,1], the forward matrix gives |L| <= 1.009.
  // Rewriting its zero-sum a/b rows as root differences gives |a| < 2.429
  // and |b| < 0.809. This sampled RGB grid checks those derived bounds.
  for (std::uint16_t red = 0U; red <= 255U; red += 17U) {
    for (std::uint16_t green = 0U; green <= 255U; green += 17U) {
      for (std::uint16_t blue = 0U; blue <= 255U; blue += 17U) {
        const auto lab = prism::controller_detail::ToOklab(RgbColor{
          static_cast<std::uint8_t>(red), static_cast<std::uint8_t>(green),
          static_cast<std::uint8_t>(blue)});
        EXPECT_LE(std::abs(static_cast<int>(lab.l)), 8270);
        EXPECT_LE(std::abs(static_cast<int>(lab.a)), 19910);
        EXPECT_LE(std::abs(static_cast<int>(lab.b)), 6635);
      }
    }
  }
}

}  // namespace
