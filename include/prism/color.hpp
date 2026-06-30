#ifndef PRISM_COLOR_HPP_
#define PRISM_COLOR_HPP_

#include <cstdint>

namespace prism {

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

/// @brief Named preset color palette.
///
/// Each enumerator carries a packed 24-bit RGB value in the lower 24 bits
/// of the underlying uint32_t.
enum class Color : std::uint32_t {
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

/// @brief Unpack a named Color preset into its RGB components.
/// @param color Named preset color.
/// @return Equivalent RgbColor extracted from the packed 0xRRGGBB
///     representation.
constexpr RgbColor ToRgb(Color color) noexcept {
  const auto raw = static_cast<std::uint32_t>(color);
  return RgbColor{
    static_cast<std::uint8_t>((raw >> 16) & 0xFF),
    static_cast<std::uint8_t>((raw >> 8) & 0xFF),
    static_cast<std::uint8_t>(raw & 0xFF),
  };
}
}  // namespace prism

#endif /* PRISM_COLOR_HPP_ */