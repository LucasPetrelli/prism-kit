#ifndef PRISM_SET_MULTIPLE_COLOR_HSV_HPP_
#define PRISM_SET_MULTIPLE_COLOR_HSV_HPP_

#include "prism/color.hpp"
#include "prism/controller_instruction.hpp"

namespace prism {

/// @brief Instruction that sets a range of pixels from an HSV color.
///
/// The HSV color is converted to RGB on execute so the underlying Strip
/// interface always receives RGB.
class SetMultipleColorHsv : public ControllerInstruction {
 public:
  SetMultipleColorHsv();

  /// @brief Construct from a serialized wire-format payload.
  /// @param payload Source payload to unpack.
  explicit SetMultipleColorHsv(const SetMultipleColorHsvPayload& payload);

  /// @brief Execute the HSV-to-RGB conversion and fill the range.
  /// @param timestamp Timestamp shared by this controller progress tick; this
  ///     instant instruction does not use it.
  /// @return 0 (instant instruction — always completes immediately).
  std::uint32_t Execute(std::uint32_t timestamp) override;

  /// @brief Write a description including HSV and range into a buffer.
  void ToString(char* buf, std::size_t size) const override;

  /// @brief HSV color to convert and apply.
  color::HsvColor color{};
  /// @brief Zero-based [start, end) pixel range.
  Range range{0U, 0U};
};

}  // namespace prism

#endif  // PRISM_SET_MULTIPLE_COLOR_HSV_HPP_
