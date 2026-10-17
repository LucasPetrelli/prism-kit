#ifndef PRISM_SET_MULTIPLE_COLOR_HPP_
#define PRISM_SET_MULTIPLE_COLOR_HPP_

#include "prism/color.hpp"
#include "prism/controller_instruction.hpp"

namespace prism {

/// @brief Instruction that sets a range of pixels to a single preset color.
class SetMultipleColor : public ControllerInstruction {
 public:
  SetMultipleColor();

  /// @brief Construct from a serialized wire-format payload.
  /// @param payload Source payload to unpack.
  explicit SetMultipleColor(const SetMultipleColorPayload& payload);

  /// @brief Execute the fill-and-show operation on the bound strip.
  /// @param timestamp Timestamp shared by this controller progress tick; this
  ///     instant instruction does not use it.
  /// @return 0 (instant instruction — always completes immediately).
  std::uint32_t Execute(std::uint32_t timestamp) override;

  /// @brief Write a description including color and range into a buffer.
  void ToString(char* buf, std::size_t size) const override;

  /// @brief RGB color to apply.
  color::RgbColor color{};
  /// @brief Zero-based [start, end) pixel range.
  Range range{0U, 0U};
};

}  // namespace prism

#endif  // PRISM_SET_MULTIPLE_COLOR_HPP_
