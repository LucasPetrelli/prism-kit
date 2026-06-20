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

/// @brief Tag identifying the concrete instruction type.
enum class InstructionTag : std::uint8_t {
  /// @brief Range-fill instruction (SetMultipleColor).
  kSetMultipleColor,
  /// @brief Single-pixel instruction (SetSingleColor).
  kSetSingleColor,
};

/// @brief Half-open [start, end) pixel index range.
struct Range {
  /// @brief Zero-based start index (inclusive).
  std::uint32_t start;
  /// @brief Zero-based end index (exclusive).
  std::uint32_t end;
};

/// @brief Polymorphic base for a single queued controller instruction.
class ControllerInstruction {
 public:
  ControllerInstruction(const ControllerInstruction&) = default;
  ControllerInstruction& operator=(const ControllerInstruction&) = delete;
  virtual ~ControllerInstruction() = default;

  /// @brief Execute this instruction against its bound strip.
  virtual void Execute() const = 0;

  /// @brief Return the tag identifying this instruction's concrete type.
  /// @return InstructionTag value set by the derived-class constructor.
  InstructionTag tag() const { return tag_; }

 protected:
  ControllerInstruction() = default;
  InstructionTag tag_{};
};

/// @brief Instruction that sets a range of pixels to a single preset color.
class SetMultipleColor : public ControllerInstruction {
 public:
  SetMultipleColor() { tag_ = InstructionTag::kSetMultipleColor; }

  /// @brief Execute the fill-and-show operation on the bound strip.
  void Execute() const override;

  /// @brief Preset color to apply.
  Color color{Color::kPureRed};
  /// @brief Non-owning pointer to the target strip.
  Strip* strip{nullptr};
  /// @brief Zero-based [start, end) pixel range.
  Range range{0U, 0U};
};

/// @brief Instruction that sets a single pixel to a preset color.
class SetSingleColor : public ControllerInstruction {
 public:
  SetSingleColor() { tag_ = InstructionTag::kSetSingleColor; }

  /// @brief Execute the set-and-show operation on the bound strip.
  void Execute() const override;

  /// @brief Preset color to apply.
  Color color{Color::kPureRed};
  /// @brief Non-owning pointer to the target strip.
  Strip* strip{nullptr};
  /// @brief Zero-based pixel index within the strip.
  std::uint32_t index{0U};
};

/// @brief Variant storage for one instruction slot with active-member tracking.
///
/// Tracks which member is active via tag_ so that set(), execute() and
/// the destructor always operate on the correct type.
struct InstructionMemorySlot {
  union {
    /// @brief Active member: range-fill instruction.
    SetMultipleColor setMultipleColor;
    /// @brief Active member: single-pixel instruction.
    SetSingleColor setSingleColor;
  };

  /// @brief Tag identifying the currently-active member.
  InstructionTag tag_{};

  /// @brief Default constructor — leaves storage uninitialised, tag is empty.
  InstructionMemorySlot() : tag_{} {}

  /// @brief Destroy the active member before the slot goes out of scope.
  ~InstructionMemorySlot();

  InstructionMemorySlot(const InstructionMemorySlot&) = delete;
  InstructionMemorySlot& operator=(const InstructionMemorySlot&) = delete;

  /// @brief Activate this slot with a copy of an already-constructed
  ///     instruction.  The previously-active member is destroyed first.
  /// @param instr Pointer to the source instruction.  Must not be null.
  void set(const ControllerInstruction* instr);

  /// @brief Execute the active instruction.
  void execute() const;

 private:
  /// @brief Return a base-class pointer to the active instruction.
  ControllerInstruction* active();

  /// @brief Return a const base-class pointer to the active instruction.
  const ControllerInstruction* active() const;

  /// @brief Destroy the active member and reset the tag.
  void destroy();
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

  /// @brief Enqueue a fully-constructed instruction.
  /// @param instr Non-owning pointer to the instruction to enqueue.
  ///     Must not be null.  Ownership remains with the caller; the
  ///     Controller copies the instruction contents into its queue.
  void AddInstruction(const ControllerInstruction* instr);

  /// @brief Clear all queued instructions.
  void ResetInstructions();

  /// @brief Iterate through enqueued instructions and execute each one.
  ///
  /// @pre A valid timestamp callback must be registered before calling Run().
  void Run() const;

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
