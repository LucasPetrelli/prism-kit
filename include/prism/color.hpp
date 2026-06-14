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
}  // namespace prism

#endif /* PRISM_COLOR_HPP_ */