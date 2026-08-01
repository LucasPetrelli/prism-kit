#ifndef PRISM_CONTROLLER_HPP_
#define PRISM_CONTROLLER_HPP_

#include <cstddef>
#include <cstdint>

#include "prism/color.hpp"
#include "prism/debug.hpp"
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

/// @brief Absolute millisecond timestamp for timeline-based instruction
///     execution.  Host software sets this in the wire payload; the
///     controller picks instructions whose mark <= current time.
using Mark = std::uint16_t;

/// @brief Shared capacity constant for the instruction queue and its
///     pending view.  Both PendingInstructionQueueView::kCapacity and
///     Controller::kMaxInstruction derive from this value.
constexpr std::uint32_t kMaxInstructions = 16U;

/// @brief Tag identifying the concrete instruction type.
enum class InstructionTag : std::uint8_t {
  /// @brief Range-fill instruction (SetMultipleColor).
  kSetMultipleColor,
  /// @brief Single-pixel instruction (SetSingleColor).
  kSetSingleColor,
  /// @brief HSV range-fill instruction (SetMultipleColorHsv).
  kSetMultipleColorHsv,
  /// @brief HSV single-pixel instruction (SetSingleColorHsv).
  kSetSingleColorHsv,
};

/// @brief Return a human-readable name for an instruction tag.
/// @param tag Tag to stringify.
/// @return Pointer to a static string describing the instruction type.
const char* InstructionToString(InstructionTag tag);

/// @brief Half-open [start, end) pixel index range.
#pragma pack(push, 1)
struct Range {
  /// @brief Zero-based start index (inclusive).
  std::uint8_t start;
  /// @brief Zero-based end index (exclusive).
  std::uint8_t end;
};

/// @brief Wire-format payload for a SetMultipleColor instruction.
/// The ``mark`` field identifies the absolute ms timestamp at which this
/// instruction should start executing.
struct SetMultipleColorPayload {
  Mark mark;
  std::uint8_t r;
  std::uint8_t g;
  std::uint8_t b;
  Range range;
};

/// @brief Wire-format payload for a SetSingleColor instruction.
struct SetSingleColorPayload {
  Mark mark;
  std::uint8_t r;
  std::uint8_t g;
  std::uint8_t b;
  std::uint8_t index;
};

/// @brief Wire-format payload for a SetSingleColorHsv instruction.
struct SetSingleColorHsvPayload {
  Mark mark;
  std::uint8_t h;
  std::uint8_t s;
  std::uint8_t v;
  std::uint8_t index;
};

/// @brief Wire-format payload for a SetMultipleColorHsv instruction.
struct SetMultipleColorHsvPayload {
  Mark mark;
  std::uint8_t h;
  std::uint8_t s;
  std::uint8_t v;
  Range range;
};
#pragma pack(pop)

static_assert(sizeof(Range) == 2U, "Range must be 2 bytes (packed)");
static_assert(sizeof(SetMultipleColorPayload) == 7U,
              "SetMultipleColorPayload must be 7 bytes (mark + r,g,b,range)");
static_assert(sizeof(SetSingleColorPayload) == 6U,
              "SetSingleColorPayload must be 6 bytes (mark + r,g,b,index)");
static_assert(sizeof(SetSingleColorHsvPayload) == 6U,
              "SetSingleColorHsvPayload must be 6 bytes (mark + h,s,v,index)");
static_assert(
  sizeof(SetMultipleColorHsvPayload) == 7U,
  "SetMultipleColorHsvPayload must be 7 bytes (mark + h,s,v,range)");

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

  /// @brief Write a human-readable description of this instruction into
  ///     a caller-provided buffer.
  /// @param buf  Destination buffer.
  /// @param size Buffer capacity.
  virtual void ToString(char* buf, std::size_t size) const;

 protected:
  ControllerInstruction() = default;
  explicit ControllerInstruction(Mark m) : mark{m} {}
  InstructionTag tag_{};

 public:
  /// @brief Absolute ms timestamp marking when this instruction should
  ///     start executing.  Set by the host via the wire payload.
  Mark mark{0U};
  /// @brief Non-owning pointer to the target strip, or nullptr.
  Strip* strip{nullptr};
  /// @brief Non-owning pointer to the owning controller, or nullptr.
  ///     Set by Controller::AddInstruction.
  Controller* controller{nullptr};
};

/// @brief Instruction that sets a range of pixels to a single preset color.
class SetMultipleColor : public ControllerInstruction {
 public:
  SetMultipleColor() { tag_ = InstructionTag::kSetMultipleColor; }

  /// @brief Construct from a serialized wire-format payload.
  /// @param payload Source payload to unpack.
  explicit SetMultipleColor(const SetMultipleColorPayload& payload)
      : ControllerInstruction(payload.mark),
        color{payload.r, payload.g, payload.b},
        range(payload.range) {
    tag_ = InstructionTag::kSetMultipleColor;
  }

  /// @brief Execute the fill-and-show operation on the bound strip.
  /// @return 0 (instant instruction — always completes immediately).
  std::uint32_t Execute() override;

  /// @brief Write a description including color and range into a buffer.
  void ToString(char* buf, std::size_t size) const override;

  /// @brief RGB color to apply.
  color::RgbColor color{};
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
      : ControllerInstruction(payload.mark),
        color{payload.r, payload.g, payload.b},
        index(payload.index) {
    tag_ = InstructionTag::kSetSingleColor;
  }

  /// @brief Execute the set-and-show operation on the bound strip.
  /// @return 0 (instant instruction — always completes immediately).
  std::uint32_t Execute() override;

  /// @brief Write a description including color and index into a buffer.
  void ToString(char* buf, std::size_t size) const override;

  /// @brief RGB color to apply.
  color::RgbColor color{};
  /// @brief Zero-based pixel index within the strip.
  std::uint8_t index{0U};
};

/// @brief Instruction that sets a single pixel from an HSV color.
///
/// The HSV color is converted to RGB on execute so the underlying Strip
/// interface always receives RGB.
class SetSingleColorHsv : public ControllerInstruction {
 public:
  SetSingleColorHsv() { tag_ = InstructionTag::kSetSingleColorHsv; }

  /// @brief Construct from a serialized wire-format payload.
  /// @param payload Source payload to unpack.
  explicit SetSingleColorHsv(const SetSingleColorHsvPayload& payload)
      : ControllerInstruction(payload.mark),
        color{payload.h, payload.s, payload.v},
        index(payload.index) {
    tag_ = InstructionTag::kSetSingleColorHsv;
  }

  /// @brief Execute the HSV-to-RGB conversion and set the pixel.
  /// @return 0 (instant instruction — always completes immediately).
  std::uint32_t Execute() override;

  /// @brief Write a description including HSV and index into a buffer.
  void ToString(char* buf, std::size_t size) const override;

  /// @brief HSV color to convert and apply.
  color::HsvColor color{};
  /// @brief Zero-based pixel index within the strip.
  std::uint8_t index{0U};
};

/// @brief Instruction that sets a range of pixels from an HSV color.
///
/// The HSV color is converted to RGB on execute so the underlying Strip
/// interface always receives RGB.
class SetMultipleColorHsv : public ControllerInstruction {
 public:
  SetMultipleColorHsv() { tag_ = InstructionTag::kSetMultipleColorHsv; }

  /// @brief Construct from a serialized wire-format payload.
  /// @param payload Source payload to unpack.
  explicit SetMultipleColorHsv(const SetMultipleColorHsvPayload& payload)
      : ControllerInstruction(payload.mark),
        color{payload.h, payload.s, payload.v},
        range(payload.range) {
    tag_ = InstructionTag::kSetMultipleColorHsv;
  }

  /// @brief Execute the HSV-to-RGB conversion and fill the range.
  /// @return 0 (instant instruction — always completes immediately).
  std::uint32_t Execute() override;

  /// @brief Write a description including HSV and range into a buffer.
  void ToString(char* buf, std::size_t size) const override;

  /// @brief HSV color to convert and apply.
  color::HsvColor color{};
  /// @brief Zero-based [start, end) pixel range.
  Range range{0U, 0U};
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
    /// @brief Active member: HSV range-fill instruction.
    SetMultipleColorHsv set_multiple_color_hsv;
    /// @brief Active member: HSV single-pixel instruction.
    SetSingleColorHsv set_single_color_hsv;
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

  /// @brief Write a description of the active instruction into a buffer.
  /// @param buf  Destination buffer.
  /// @param size Buffer capacity.
  void ToString(char* buf, std::size_t size) const;

  /// @brief Return the mark of the active instruction.
  /// @return The mark value, or 0 if no instruction is active.
  Mark GetMark() const;

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

/// @brief Read-only view over the Controller's instruction array
///     exposing a mark-sorted iteration cursor (head).  Insertions are
///     O(n) where n = sorted_count_ (capped at kCapacity).
///
/// This class does not own the instruction storage — it only tracks
/// indices into the Controller's ``instructions_[]`` array.
///
/// Usage pattern:
/// @code
///   while (queue.HasNext() && queue.PeekMark() <= now) {
///     uint32_t idx = queue.Peek();
///     /* ... execute instructions_[idx] ... */
///     queue.Advance();
///   }
/// @endcode
class PendingInstructionQueueView {
 public:
  /// @brief Maximum number of entries this view can hold.
  static constexpr std::uint32_t kCapacity = kMaxInstructions;

  /// @brief Set the instruction array for mark lookups.
  /// @param instructions Non-owning pointer to the Controller's
  ///     InstructionMemorySlot array.  Must outlive this queue.
  void SetInstructions(InstructionMemorySlot* instructions) {
    instructions_ = instructions;
  }

  /// @brief Insert a slot index in ascending-mark order.
  /// @param slot_idx Index into the Controller's instructions_[] array.
  /// @pre slot_idx < kCapacity.
  void Insert(std::uint32_t slot_idx);

  /// @brief True when at least one unconsumed instruction remains.
  bool HasNext() const { return head_ < sorted_count_; }

  /// @brief Index of the next unconsumed instruction.
  /// @pre HasNext() is true.
  std::uint32_t Peek() const { return sorted_order_[head_]; }

  /// @brief Mark of the next unconsumed instruction.
  /// @pre HasNext() is true.
  Mark PeekMark() const {
    return instructions_[sorted_order_[head_]].GetMark();
  }

  /// @brief Advance the cursor past the current head.
  /// @pre HasNext() is true.
  void Advance() { ++head_; }

  /// @brief Reset cursor and clear all entries.
  void Reset() {
    sorted_count_ = 0U;
    head_ = 0U;
  }

 private:
  /// @brief Non-owning pointer to the instruction array for GetMark().
  InstructionMemorySlot* instructions_{nullptr};
  /// @brief Indices into the instruction array, sorted by ascending mark.
  std::uint32_t sorted_order_[kCapacity]{};
  /// @brief Number of valid entries in sorted_order_.
  std::uint32_t sorted_count_{0U};
  /// @brief Index of the next entry to consume.
  std::uint32_t head_{0U};
};

/// @brief High-level animation controller for a Prism Kit strip.
///
/// Accepts preset-color instructions, enqueues them in a fixed-capacity
/// ring, and executes them sequentially when Run() is called.
class Controller {
 public:
  /// @brief Maximum number of queued instructions.
  static constexpr std::uint32_t kMaxInstruction = kMaxInstructions;
  /// @brief Maximum number of concurrently-executing timed instructions.
  static constexpr std::uint32_t kMaxExecuting = kMaxInstruction;
  /// @brief Size of the stack buffer used by DebugLog to build the
  ///     prefix-tagged format string.
  static constexpr std::uint32_t kDebugLogBufferSize = 96U;

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
  ///     pick new instructions whose mark <= current timestamp.
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

  /// @brief Register a debug output sink.
  /// @param d Non-owning pointer to a Debug instance, or nullptr to
  ///     disable debug output.
  void SetDebug(Debug* d);

  /// @brief Called by color-affecting instructions to request a Show()
  ///     commit at the end of Run().  Idempotent within a single Run().
  void RequestShow();

 private:
  /// @brief Run timed instructions in the executing_ array, removing
  ///     completed entries (swap-with-last).
  void DrainExecuting();

  /// @brief Consume pending instructions whose mark <= current timestamp.
  ///     Multiple instructions may share the same mark (all execute at
  ///     the same time).  Stops at the first whose mark is in the future
  ///     — the sorted invariant guarantees all remaining instructions
  ///     have marks >= that future mark, so none qualify until time
  ///     advances.
  void PickNewInstructions();

  /// @brief Record a timeout for the end-of-run schedule callback.
  /// @param ms Timeout in milliseconds.  Only the smallest value across all
  ///     calls within a single Run() is retained.
  void ScheduleTimeout(std::uint32_t ms);

  /// @brief Write a formatted debug message through the registered debug sink.
  /// @param format Printf-style format string.
  /// @param ... Variadic arguments matching the format string.
  void DebugLog(const char* format, ...) const;

  /// @brief Log a drain-executing step with the instruction description.
  /// @param i    Index into the executing_[] array (position in the drain
  /// loop).
  /// @param idx  Index into instructions_[] for the instruction being drained.
  void DebugLogDrain(std::uint32_t i, std::uint32_t idx) const;

  /// @brief Log a pick-new-instruction step with mark and instruction desc.
  /// @param idx     Index into instructions_[] for the instruction being
  /// picked.
  /// @param elapsed Milliseconds since the first Run() call (marks are
  ///     relative offsets from that point).
  void DebugLogPick(std::uint32_t idx, std::uint32_t elapsed) const;

  /// @brief Milliseconds elapsed since the first Run() call.
  /// @pre has_run_ is true.
  std::uint32_t GetElapsed() const;

  /// @brief Non-owning pointer to the bound strip, or nullptr.
  Strip* strip_;
  /// @brief Fixed-capacity instruction queue.
  InstructionMemorySlot instructions_[kMaxInstruction];
  /// @brief Number of active instructions in the queue.
  std::uint32_t instruction_count_;
  /// @brief Mark-sorted view over pending instruction indices.
  PendingInstructionQueueView pending_;
  /// @brief Indices of instruction slots currently under execution (timed).
  std::uint32_t executing_[kMaxExecuting]{};
  /// @brief Number of entries in executing_.
  std::uint32_t executing_count_{0U};

  /// @brief Set by instructions via RequestShow(); cleared at each Run().
  bool show_requested_{false};
  /// @brief True after the first Run() call — used to snapshot the start
  ///     timestamp so marks are treated as offsets from that point.
  bool has_run_{false};
  /// @brief Timestamp captured on the first Run() call.  Marks are
  ///     relative offsets from this value.
  std::uint32_t start_time_{0U};
  /// @brief Timestamp callback for timing-aware execution, or nullptr.
  TimestampCallback get_timestamp_;
  /// @brief Schedule callback for timed-instruction re-arming, or nullptr.
  ScheduleCallback schedule_next_run_;
  /// @brief Non-owning pointer to the debug output sink, or nullptr.
  Debug* debug_;
  /// @brief Cached minimum timeout across one Run(); 0 means no timeout.
  std::uint32_t min_scheduled_timeout_{0U};
};

}  // namespace prism

#endif /* PRISM_CONTROLLER_HPP_ */
