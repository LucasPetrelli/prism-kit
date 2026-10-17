#ifndef PRISM_FADE_HPP_
#define PRISM_FADE_HPP_

#include "prism/color.hpp"
#include "prism/controller_instruction.hpp"

namespace prism {

/// @brief Scale one staged pixel's HSV Value over an elapsed duration.
///
/// The staged RGB color is captured when the instruction becomes due. Each
/// update scales that captured RGB proportionally so integer HSV round-trips
/// do not accumulate quantization error.
class Fade : public ControllerInstruction {
 public:
  /// @brief Construct a Fade from its fixed-width payload.
  /// @param payload Source payload.
  explicit Fade(const FadePayload& payload);

  /// @brief Capture the staged color or advance the elapsed-time interpolation.
  /// @param timestamp Shared progress-tick timestamp used for activation,
  ///     interpolation, and update-grid alignment.
  /// @return Milliseconds until the next shared update or exact endpoint.
  std::uint32_t Execute(std::uint32_t timestamp) override;

  /// @brief Describe this Fade for controller diagnostics.
  void ToString(char* buf, std::size_t size) const override;

  /// @brief Reset activation state before replaying the instruction.
  void ResetForReplay() override;

  /// @brief Report whether a nonzero duration bounds repeated execution.
  bool HasPositiveSchedulingBoundary() const override;

  /// @brief Percentage multiplier applied to the captured Value.
  std::uint16_t percent{0U};
  /// @brief Fade duration in milliseconds.
  std::uint16_t duration_ms{0U};
  /// @brief Zero-based pixel index within the strip.
  std::uint8_t index{0U};

 private:
  /// @brief Whether the initial staged color has been captured.
  bool started_{false};
  /// @brief Timestamp at which the staged color was captured.
  std::uint32_t start_time_{0U};
  /// @brief RGB color captured at activation.
  color::RgbColor start_color_{};
  /// @brief Maximum channel of the captured color (HSV Value).
  std::uint8_t start_value_{0U};
  /// @brief Scaled, saturated target Value.
  std::uint8_t target_value_{0U};
};

}  // namespace prism

#endif  // PRISM_FADE_HPP_
