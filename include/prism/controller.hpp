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

/// @brief Callback invoked when a timed instruction yields a non-zero
///     duration.  The owner should schedule a subsequent Run() call
///     after the given delay.
/// @param delay_ms Milliseconds until the next Run() should occur.
using ScheduleCallback = void (*)(std::uint32_t delay_ms);

/// @brief Tag identifying the concrete instruction type.
enum class InstructionTag : std::uint8_t {
  /// @brief Range-fill instruction (SetMultipleColor).
  kSetMultipleColor,
  /// @brief Single-pixel instruction (SetSingleColor).
  kSetSingleColor,
  /// @brief Time-delay instruction (Delay).
  kDelay,
};

/// @brief Half-open [start, end) pixel index range.
struct Range {
  /// @brief Zero-based start index (inclusive).
  std::uint8_t start;
  /// @brief Zero-based end index (exclusive).
  std::uint8_t end;
};

/// @brief Wire-format payload for a SetMultipleColor instruction.
struct SetMultipleColorPayload {
  std::uint8_t r;
  std::uint8_t g;
  std::uint8_t b;
  Range range;
};

/// @brief Wire-format payload for a SetSingleColor instruction.
struct SetSingleColorPayload {
  std::uint8_t r;
  std::uint8_t g;
  std::uint8_t b;
  std::uint8_t index;
};

class Controller;

/// @brief Polymorphic base for a single queued controller instruction.
class ControllerInstruction {
 public:
  ControllerInstruction(const ControllerInstruction&) = default;
  ControllerInstruction& operator=(const ControllerInstruction&) = delete;
  virtual ~ControllerInstruction() = default;

  /// @brief Execute this instruction against its bound strip.
  /// @return 0 if the instruction completed, or a positive duration in
  ///     milliseconds after which Execute() should be called again.
  virtual std::uint32_t Execute() = 0;

  /// @brief Return the tag identifying this instruction's concrete type.
  /// @return InstructionTag value set by the derived-class constructor.
  InstructionTag Tag() const { return tag_; }

 protected:
  ControllerInstruction() = default;
  InstructionTag tag_{};

 public:
  /// @brief Non-owning pointer to the target strip, or nullptr.
  Strip* strip{nullptr};
  /// @brief Non-owning pointer to the owning controller, or nullptr.
  ///     Set by Controller::AddInstruction.  Timed instructions use this
  ///     to call Block() / Unblock() on the controller.
  Controller* controller{nullptr};
};

/// @brief Instruction that sets a range of pixels to a single preset color.
class SetMultipleColor : public ControllerInstruction {
 public:
  SetMultipleColor() { tag_ = InstructionTag::kSetMultipleColor; }

  /// @brief Construct from a serialized wire-format payload.
  /// @param payload Source payload to unpack.
  explicit SetMultipleColor(const SetMultipleColorPayload& payload)
      : color{payload.r, payload.g, payload.b}, range(payload.range) {
    tag_ = InstructionTag::kSetMultipleColor;
  }

  /// @brief Execute the fill-and-show operation on the bound strip.
  /// @return 0 (instant instruction — always completes immediately).
  std::uint32_t Execute() override;

  /// @brief RGB color to apply.
  RgbColor color{};
  /// @brief Zero-based [start, end) pixel range.
  Range range{0U, 0U};
};

/// @brief Instruction that sets a single pixel to a preset color.
class SetSingleColor : public ControllerInstruction {
 public:
  SetSingleColor() { tag_ = InstructionTag::kSetSingleColor; }

  /// @brief Construct from a serialized wire-format payload.
  /// @param payload Source payload to unpack.
  explicit SetSingleColor(const SetSingleColorPayload& payload)
      : color{payload.r, payload.g, payload.b}, index(payload.index) {
    tag_ = InstructionTag::kSetSingleColor;
  }

  /// @brief Execute the set-and-show operation on the bound strip.
  /// @return 0 (instant instruction — always completes immediately).
  std::uint32_t Execute() override;

  /// @brief RGB color to apply.
  RgbColor color{};
  /// @brief Zero-based pixel index within the strip.
  std::uint8_t index{0U};
};

/// @brief Delay that blocks instruction execution for a fixed duration.
///
/// The first call to Execute() marks the controller as blocked and returns
/// the configured delay duration.  The controller re-arms via the schedule
/// callback.  On the second call (after the timer fires), the Delay
/// unblocks the controller and returns 0 (completed).
class Delay : public ControllerInstruction {
 public:
  Delay() { tag_ = InstructionTag::kDelay; }

  /// @brief Construct with a fixed delay duration.
  /// @param delay_ms Delay duration in milliseconds.
  explicit Delay(std::uint32_t delay_ms) : delay_ms_(delay_ms) {
    tag_ = InstructionTag::kDelay;
  }

  /// @brief Execute one step of the delay.
  /// @return delay_ms on first call, the remaining duration if the delay
  ///     has not yet elapsed, or 0 on completion.
  std::uint32_t Execute() override;

 private:
  /// @brief Configured delay duration in milliseconds.
  std::uint32_t delay_ms_{0U};

  /// @brief Timestamp captured at the first Execute() call.
  /// @note 0 also serves as the first-call marker.
  std::uint32_t start_time_ms_{0U};
};

/// @brief Variant storage for one instruction slot with active-member tracking.
///
/// Tracks which member is active via tag_ so that set(), execute() and
/// the destructor always operate on the correct type.
struct InstructionMemorySlot {
  union {
    /// @brief Active member: range-fill instruction.
    SetMultipleColor set_multiple_color;
    /// @brief Active member: single-pixel instruction.
    SetSingleColor set_single_color;
    /// @brief Active member: time-delay instruction.
    Delay delay;
  };

  /// @brief Tag identifying the currently-active member.
  InstructionTag tag{};

  /// @brief Default constructor — leaves storage uninitialised, tag is empty.
  InstructionMemorySlot() : tag{} {}

  /// @brief Destroy the active member before the slot goes out of scope.
  ~InstructionMemorySlot();

  InstructionMemorySlot(const InstructionMemorySlot&) = delete;
  InstructionMemorySlot& operator=(const InstructionMemorySlot&) = delete;

  /// @brief Activate this slot with a copy of an already-constructed
  ///     instruction.  The previously-active member is destroyed first.
  /// @param instr Pointer to the source instruction.  Must not be null.
  void Set(const ControllerInstruction* instr);

  /// @brief Set the strip pointer on the active instruction.
  /// @param s Non-owning pointer to the strip to bind.
  void SetStrip(Strip* s);

  /// @brief Execute the active instruction.
  /// @return 0 if completed, or a positive duration in ms until the next call.
  std::uint32_t Execute();

  /// @brief Set the controller pointer on the active instruction.
  /// @param c Non-owning pointer to the owning controller.
  void SetController(Controller* c);

 private:
  /// @brief Return a base-class pointer to the active instruction.
  ControllerInstruction* Active();

  /// @brief Return a const base-class pointer to the active instruction.
  const ControllerInstruction* Active() const;

  /// @brief Destroy the active member and reset the tag.
  void Destroy();
};

/// @brief High-level animation controller for a Prism Kit strip.
///
/// Accepts preset-color instructions, enqueues them in a fixed-capacity
/// ring, and executes them sequentially when Run() is called.
class Controller {
 public:
  /// @brief Maximum number of queued instructions.
  static constexpr std::uint32_t kMaxInstruction = 16U;
  /// @brief Maximum number of concurrently-executing timed instructions.
  static constexpr std::uint32_t kMaxExecuting = kMaxInstruction;

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

  /// @brief Iterate through instructions: drain executing array first, then
  ///     pick new instructions from the queue (if not blocked).
  ///
  /// @pre A valid timestamp callback must be registered before calling Run().
  void Run();

  /// @brief Register a timestamp callback for timing-aware execution.
  /// @param callback Function returning the current timestamp in milliseconds.
  void SetTimestampCallback(TimestampCallback callback);

  /// @brief Query the current monotonically-increasing timestamp.
  /// @return Current timestamp in milliseconds from the registered callback,
  ///     or 0 if none is set.
  std::uint32_t GetTimestamp() const;

  /// @brief Register a schedule callback for timed-instruction re-arming.
  /// @param callback Function called with the duration in ms after which
  ///     Run() should be called again.  May be nullptr (no re-arm).
  void SetScheduleCallback(ScheduleCallback callback);

  /// @brief Increment the blocking counter.  While the counter is > 0,
  ///     Run() will not pick up new instructions from the queue.
  void Block();

  /// @brief Decrement the blocking counter.  When the counter reaches 0,
  ///     Run() may pick up new instructions again.
  void Unblock();

  /// @brief Check whether the controller is currently blocked.
  /// @return True when Block() has been called more times than Unblock().
  bool IsBlocked() const;

  /// @brief Called by color-affecting instructions to request a Show()
  ///     commit at the end of Run().  Idempotent within a single Run().
  void RequestShow();

 private:
  /// @brief Run timed instructions in the executing_ array, removing
  ///     completed entries (swap-with-last).
  void DrainExecuting();

  /// @brief Consume new instructions from head_index_ while !IsBlocked().
  void PickNewInstructions();

  /// @brief Record a timeout for the end-of-run schedule callback.
  /// @param ms Timeout in milliseconds.  Only the smallest value across all
  ///     calls within a single Run() is retained.
  void ScheduleTimeout(std::uint32_t ms);

  /// @brief Non-owning pointer to the bound strip, or nullptr.
  Strip* strip_;
  /// @brief Fixed-capacity instruction queue.
  InstructionMemorySlot instructions_[kMaxInstruction];
  /// @brief Number of active instructions in the queue.
  std::uint32_t instruction_count_;
  /// @brief Index of the next instruction in instructions_[] to consume.
  std::uint32_t head_index_{0U};
  /// @brief Indices of instruction slots currently under execution (timed).
  std::uint32_t executing_[kMaxExecuting]{};
  /// @brief Number of entries in executing_.
  std::uint32_t executing_count_{0U};
  /// @brief Re-entrant blocking counter.
  std::uint8_t block_count_{0U};
  /// @brief Set by instructions via RequestShow(); cleared at each Run().
  bool show_requested_{false};
  /// @brief Timestamp callback for timing-aware execution, or nullptr.
  TimestampCallback get_timestamp_;
  /// @brief Schedule callback for timed-instruction re-arming, or nullptr.
  ScheduleCallback schedule_next_run_;
  /// @brief Cached minimum timeout across one Run(); 0 means no timeout.
  std::uint32_t min_scheduled_timeout_{0U};
};

}  // namespace prism

#endif /* PRISM_CONTROLLER_HPP_ */
