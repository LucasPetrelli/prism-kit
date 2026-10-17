#ifndef PRISM_GRADIENT_HPP_
#define PRISM_GRADIENT_HPP_

#include <cstddef>
#include <cstdint>

#include "prism/color.hpp"
#include "prism/controller_instruction.hpp"

namespace prism {

/// @brief Interpolate one staged RGB pixel to a target through OKLab.
///
/// The staged source is captured when the instruction becomes due. Samples
/// are based on elapsed time and use the controller-configured refresh period
/// (20 ms by default) relative to activation, with an additional update at the
/// exact duration boundary.
class Gradient : public ControllerInstruction {
 public:
  /// @brief Construct a Gradient from its fixed-width payload.
  /// @param payload Source payload.
  explicit Gradient(const GradientPayload& payload);

  /// @brief Capture the staged source or advance the elapsed-time transition.
  /// @param timestamp Shared progress-tick timestamp used for activation,
  ///     interpolation, and rescheduling.
  /// @return Milliseconds until the next relative grid point or endpoint.
  std::uint32_t Execute(std::uint32_t timestamp) override;

  /// @brief Describe this Gradient for controller diagnostics.
  void ToString(char* buf, std::size_t size) const override;

  /// @brief Reset activation state before replaying the instruction.
  void ResetForReplay() override;

  /// @brief Report whether a nonzero duration bounds repeated execution.
  bool HasPositiveSchedulingBoundary() const override;

  /// @brief Exact sRGB endpoint requested by the instruction.
  color::RgbColor target{};
  /// @brief Transition duration in milliseconds.
  std::uint16_t duration_ms{0U};
  /// @brief Zero-based pixel index within the strip.
  std::uint8_t index{0U};

 private:
  /// @brief Whether source capture and timing initialization have occurred.
  bool started_{false};
  /// @brief Timestamp at which the staged source was captured.
  std::uint32_t start_time_{0U};
  /// @brief Cached source OKLab coordinates in signed Q2.13.
  std::int16_t source_l_{0};
  std::int16_t source_a_{0};
  std::int16_t source_b_{0};
  /// @brief Cached target OKLab coordinates in signed Q2.13.
  std::int16_t target_l_{0};
  std::int16_t target_a_{0};
  std::int16_t target_b_{0};
};

}  // namespace prism

#endif  // PRISM_GRADIENT_HPP_
