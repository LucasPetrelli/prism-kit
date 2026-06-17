#ifndef PRISM_CONTROLLER_HPP_
#define PRISM_CONTROLLER_HPP_

#include <cstdint>

#include "prism/color.hpp"
#include "prism/strip.hpp"

namespace prism {

/// @brief Callback returning a monotonically-increasing timestamp in
///     milliseconds.
/// @return Current timestamp value.
using TimestampCallback = std::uint32_t (*)();

/// @brief Polymorphic base for a single queued controller instruction.
class ControllerInstruction {
 public:
  ControllerInstruction(const ControllerInstruction&) = delete;
  ControllerInstruction& operator=(const ControllerInstruction&) = delete;
  virtual ~ControllerInstruction() = default;

  /// @brief Execute this instruction against its bound strip.
  virtual void Execute() = 0;

 protected:
  ControllerInstruction() = default;
};

/// @brief Instruction that sets a range of pixels to a single preset color.
class SetMultipleColor : public ControllerInstruction {
 public:
  /// @brief Execute the fill-and-show operation on the bound strip.
  void Execute() override;

  /// @brief Preset color to apply.
  Color color{Color::kPureRed};
  /// @brief Non-owning pointer to the target strip.
  Strip* strip{nullptr};
  /// @brief Zero-based [start, end) pixel range.
  std::uint32_t range[2]{0U, 0U};
};

/// @brief Instruction that sets a single pixel to a preset color.
class SetSingleColor : public ControllerInstruction {
 public:
  /// @brief Execute the set-and-show operation on the bound strip.
  void Execute() override;

  /// @brief Preset color to apply.
  Color color{Color::kPureRed};
  /// @brief Non-owning pointer to the target strip.
  Strip* strip{nullptr};
  /// @brief Zero-based pixel index within the strip.
  std::uint32_t index{0U};
};

/// @brief Variant storage for one instruction slot.
///
/// Only the actively-constructed member may be accessed.  The union owns
/// construction via the set() overloads and dispatch via execute().
union InstructionMemorySlot {
  /// @brief Active member: range-fill instruction.
  SetMultipleColor setMultipleColor;
  /// @brief Active member: single-pixel instruction.
  SetSingleColor setSingleColor;

  /// @brief Trivial default constructor — leaves memory uninitialized.
  InstructionMemorySlot() {}
  /// @brief Trivial destructor — caller must destroy the active member
  ///     before the slot goes out of scope.
  ~InstructionMemorySlot() {}

  /// @brief Activate this slot with a copy of an already-constructed
  ///     range-fill instruction.
  /// @param instr Pointer to the source instruction.  Must not be null.
  void set(const SetMultipleColor* instr);

  /// @brief Activate this slot with a copy of an already-constructed
  ///     single-pixel instruction.
  /// @param instr Pointer to the source instruction.  Must not be null.
  void set(const SetSingleColor* instr);

  /// @brief Execute the active instruction through the shared base class.
  ///
  /// Both members inherit from ControllerInstruction and share the same
  /// vtable-pointer layout, so dispatch is safe regardless of which
  /// member was last constructed.
  void execute();
};

/// @brief High-level animation controller for a Prism Kit strip.
///
/// Accepts preset-color instructions, enqueues them in a fixed-capacity
/// ring, and executes them sequentially when Run() is called.
class Controller {
 public:
  /// @brief Maximum number of queued instructions.
  static constexpr std::uint32_t kMaxInstruction = 16U;

  Controller(const Controller&) = delete;
  Controller& operator=(const Controller&) = delete;

  /// @brief Construct a Controller with no bound strip or timestamp callback.
  Controller();

  /// @brief Bind a strip for subsequent instructions.
  /// @param strip Non-owning pointer to the Prism Kit strip to control.
  void SetStrip(Strip* strip);

  /// @brief Enqueue a preset-color instruction.
  ///
  /// @param color Preset color to apply.
  /// @param index Zero-based pixel index for a single-pixel instruction, or
  ///     the exclusive end-index for a range instruction (start = 0).
  /// @note The Controller may internally decide whether to create a
  ///     SetSingleColor or SetMultipleColor slot based on the value
  ///     of @p index and any additional future API.
  void AddInstruction(Color color, std::uint32_t index);

  /// @brief Clear all queued instructions.
  void ResetInstructions();

  /// @brief Iterate through enqueued instructions and execute each one.
  ///
  /// @pre A valid timestamp callback must be registered before calling Run().
  void Run();

  /// @brief Register a timestamp callback for timing-aware execution.
  /// @param callback Function returning the current timestamp in milliseconds.
  void SetTimestampCallback(TimestampCallback callback);

 private:
  /// @brief Non-owning pointer to the bound strip, or nullptr.
  Strip* strip_;
  /// @brief Fixed-capacity instruction queue.
  InstructionMemorySlot instructions_[kMaxInstruction];
  /// @brief Number of active instructions in the queue.
  std::uint32_t instruction_count_;
  /// @brief Timestamp callback for timing-aware execution, or nullptr.
  TimestampCallback get_timestamp_;
};

}  // namespace prism

#endif /* PRISM_CONTROLLER_HPP_ */
