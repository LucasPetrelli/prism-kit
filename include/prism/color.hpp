#ifndef PRISM_COLOR_HPP_
#define PRISM_COLOR_HPP_

#include <algorithm>
#include <cstdint>

namespace prism::color {

/// @brief Logical RGB color owned by Prism Kit strip interfaces.
struct RgbColor {
  /// @brief Red intensity component.
  std::uint8_t red;
  /// @brief Green intensity component.
  std::uint8_t green;
  /// @brief Blue intensity component.
  std::uint8_t blue;
};

/// @brief Equality comparison for RgbColor.
constexpr bool operator==(const RgbColor& a, const RgbColor& b) noexcept {
  return a.red == b.red && a.green == b.green && a.blue == b.blue;
}

/// @brief Hue-saturation-value color representation.
///
/// Hue is stored as a 0–255 value linearly mapped from the standard 0–360°
/// range.  The conversion functions rescale internally so callers always
/// think in degree-like terms when constructing HSV colors.
struct HsvColor {
  /// @brief Hue component, 0–255 (maps to 0–360°).
  std::uint8_t h;
  /// @brief Saturation component, 0–255.
  std::uint8_t s;
  /// @brief Value (brightness) component, 0–255.
  std::uint8_t v;
};

/// @brief Equality comparison for HsvColor.
constexpr bool operator==(const HsvColor& a, const HsvColor& b) noexcept {
  return a.h == b.h && a.s == b.s && a.v == b.v;
}

/// @brief Named preset color palette.
///
/// Each enumerator carries a packed 24-bit RGB value in the lower 24 bits
/// of the underlying uint32_t.
enum class Preset : std::uint32_t {
  /// @brief Warm white (#FF952B).
  kWarmWhite = 0xFF952B,
  /// @brief Warm white alternate shade (#FFB266).
  kWarmWhiteAlt = 0xFFB266,
  /// @brief Cool white (#E0F7F4).
  kCoolWhite = 0xE0F7F4,
  /// @brief Pure white (#FFFFFF).
  kPureWhite = 0xFFFFFF,
  /// @brief Nightlight amber (#FF5500).
  kNightlightAmber = 0xFF5500,
  /// @brief Pure red (#FF0000).
  kPureRed = 0xFF0000,
  /// @brief Pure green (#00FF00).
  kPureGreen = 0x00FF00,
  /// @brief Pure blue (#0000FF).
  kPureBlue = 0x0000FF,
  /// @brief Cyberpunk pink (#FF0055).
  kCyberpunkPink = 0xFF0055,
  /// @brief Ice blue / cyan (#00FFFF).
  kIceBlue = 0x00FFFF,
  /// @brief Electric purple (#6600FF).
  kElectricPurple = 0x6600FF,
  /// @brief Emerald / mint (#00FF66).
  kEmerald = 0x00FF66,
  /// @brief Halloween orange (#FF3300).
  kHalloweenOrange = 0xFF3300,
  /// @brief Christmas gold (#FFD700).
  kChristmasGold = 0xFFD700,
};

/// @brief Unpack a named Preset into its RGB components.
/// @param preset Named preset color.
/// @return Equivalent RgbColor extracted from the packed 0xRRGGBB
///     representation.
constexpr RgbColor ToRgb(Preset preset) noexcept {
  const auto raw = static_cast<std::uint32_t>(preset);
  return RgbColor{
    static_cast<std::uint8_t>((raw >> 16) & 0xFF),
    static_cast<std::uint8_t>((raw >> 8) & 0xFF),
    static_cast<std::uint8_t>(raw & 0xFF),
  };
}

/// @brief Convert HSV to RGB using integer arithmetic.
/// @param hsv HSV color (h 0–255 mapped to 0–360°, s 0–255, v 0–255).
/// @return Equivalent RgbColor.
constexpr RgbColor HsvToRgb(HsvColor hsv) noexcept {
  if (hsv.s == 0U) {
    // Achromatic (gray).
    return RgbColor{hsv.v, hsv.v, hsv.v};
  }

  // Scale hue from 0–255 to 0–359 degrees.
  // Wrap 360° back to 0° so sector stays in [0, 5].
  const std::uint16_t scaled_h =
    (static_cast<std::uint16_t>(hsv.h) * 360U) / 255U;
  const std::uint8_t sector =
    static_cast<std::uint8_t>((scaled_h >= 360U) ? 0U : scaled_h / 60U);
  const std::uint8_t f =
    static_cast<std::uint8_t>((scaled_h >= 360U) ? 0U : scaled_h % 60U);

  const std::uint16_t v = hsv.v;
  const std::uint16_t s = hsv.s;

  const std::uint8_t p = static_cast<std::uint8_t>(v * (255U - s) / 255U);
  const std::uint8_t q =
    static_cast<std::uint8_t>(v * (255U - ((s * f) / 60U)) / 255U);
  const std::uint8_t t =
    static_cast<std::uint8_t>(v * (255U - ((s * (60U - f)) / 60U)) / 255U);

  switch (sector) {
    case 0U:
      return RgbColor{static_cast<std::uint8_t>(v), t, p};
    case 1U:
      return RgbColor{q, static_cast<std::uint8_t>(v), p};
    case 2U:
      return RgbColor{p, static_cast<std::uint8_t>(v), t};
    case 3U:
      return RgbColor{p, q, static_cast<std::uint8_t>(v)};
    case 4U:
      return RgbColor{t, p, static_cast<std::uint8_t>(v)};
    default:
      return RgbColor{static_cast<std::uint8_t>(v), p, q};
  }
}

/// @brief Convert RGB to HSV using integer arithmetic.
/// @param rgb RGB color.
/// @return Equivalent HsvColor (h 0–255 mapped from 0–360°).
constexpr HsvColor RgbToHsv(RgbColor rgb) noexcept {
  const std::uint8_t mx = std::max({rgb.red, rgb.green, rgb.blue});
  const std::uint8_t mn = std::min({rgb.red, rgb.green, rgb.blue});
  const std::uint8_t delta = mx - mn;

  // Value.
  const std::uint8_t v = mx;

  // Saturation.
  const std::uint8_t s = (mx == 0U)
                           ? 0U
                           : static_cast<std::uint8_t>(
                               static_cast<std::uint16_t>(delta) * 255U / mx);

  // Hue (0–255 scaled from 0–360°).
  std::uint8_t h = 0U;
  if (delta != 0U) {
    std::int16_t h_deg = 0;
    if (mx == rgb.red) {
      h_deg = ((static_cast<std::int16_t>(rgb.green) -
                static_cast<std::int16_t>(rgb.blue)) *
               60 / static_cast<std::int16_t>(delta));
      if (h_deg < 0) {
        h_deg += 360;
      }
    } else if (mx == rgb.green) {
      h_deg = ((static_cast<std::int16_t>(rgb.blue) -
                static_cast<std::int16_t>(rgb.red)) *
               60 / static_cast<std::int16_t>(delta)) +
              120;
    } else {
      h_deg = ((static_cast<std::int16_t>(rgb.red) -
                static_cast<std::int16_t>(rgb.green)) *
               60 / static_cast<std::int16_t>(delta)) +
              240;
    }
    // Round to nearest so h_deg=359 maps to 255 instead of 254.
    h = static_cast<std::uint8_t>(
      ((static_cast<std::uint16_t>(h_deg) * 255U) + 180U) / 360U);
  }

  return HsvColor{h, s, v};
}

}  // namespace prism::color

#endif /* PRISM_COLOR_HPP_ */