#ifndef PRISM_SET_SINGLE_COLOR_HPP_
#define PRISM_SET_SINGLE_COLOR_HPP_

#include "prism/color.hpp"
#include "prism/controller_instruction.hpp"

namespace prism {

/// @brief Instruction that sets a single pixel to a preset color.
class SetSingleColor : public ControllerInstruction {
 public:
  SetSingleColor();

  /// @brief Construct from a serialized wire-format payload.
  /// @param payload Source payload to unpack.
  explicit SetSingleColor(const SetSingleColorPayload& payload);

  /// @brief Execute the set-and-show operation on the bound strip.
  /// @param timestamp Timestamp shared by this controller progress tick; this
  ///     instant instruction does not use it.
  /// @return 0 (instant instruction — always completes immediately).
  std::uint32_t Execute(std::uint32_t timestamp) override;

  /// @brief Write a description including color and index into a buffer.
  void ToString(char* buf, std::size_t size) const override;

  /// @brief RGB color to apply.
  color::RgbColor color{};
  /// @brief Zero-based pixel index within the strip.
  std::uint8_t index{0U};
};

}  // namespace prism

#endif  // PRISM_SET_SINGLE_COLOR_HPP_
