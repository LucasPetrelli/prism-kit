#ifndef PRISM_APP_CONTROLLER_OKLAB_HPP_
#define PRISM_APP_CONTROLLER_OKLAB_HPP_

#include <cstdint>

#include "prism/color.hpp"

namespace prism::controller_detail {

/// @brief Rectangular OKLab coordinates in signed Q2.13 format.
///
/// Values produced from sRGB inputs fit this range by the coefficient bounds
/// documented in oklab.cpp. This type stays private to APP implementation.
struct OklabColor {
  std::int16_t l;
  std::int16_t a;
  std::int16_t b;
};

/// @brief Convert an sRGB byte triplet to fixed-point OKLab.
/// @param rgb sRGB channels in the project's logical RGB byte order.
/// @return Q2.13 OKLab coordinates.
OklabColor ToOklab(color::RgbColor rgb) noexcept;

/// @brief Convert a supported fixed-point OKLab sample to sRGB bytes.
/// @param lab Coordinates returned by ToOklab or Interpolate.
/// @return sRGB channels after linear-sRGB clipping and deterministic encoding.
color::RgbColor FromOklab(OklabColor lab) noexcept;

/// @brief Interpolate two OKLab samples with deterministic nearest rounding.
/// @param source Starting OKLab sample.
/// @param target Destination OKLab sample.
/// @param elapsed_ms Elapsed portion of the duration.
/// @param duration_ms Total duration; zero selects the target immediately.
/// @return The source at zero elapsed, the target at completion, otherwise
///     the rounded linear OKLab sample.
OklabColor Interpolate(OklabColor source, OklabColor target,
                       std::uint16_t elapsed_ms,
                       std::uint16_t duration_ms) noexcept;

/// @brief Sample a complete RGB-to-RGB OKLab path, preserving exact endpoints.
///
/// This convenience helper is useful to internal callers that do not retain
/// converted endpoints. Timed instructions can cache ToOklab results and use
/// Interpolate plus FromOklab for intermediate samples.
/// @param source Starting sRGB color.
/// @param target Destination sRGB color.
/// @param elapsed_ms Elapsed portion of the duration.
/// @param duration_ms Total duration; zero selects the target immediately.
/// @return The exact source or target at endpoints, otherwise an sRGB sample
///     encoded from the interpolated OKLab coordinates.
color::RgbColor InterpolateRgb(color::RgbColor source, color::RgbColor target,
                               std::uint16_t elapsed_ms,
                               std::uint16_t duration_ms) noexcept;

}  // namespace prism::controller_detail

#endif  // PRISM_APP_CONTROLLER_OKLAB_HPP_
