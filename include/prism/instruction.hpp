#ifndef PRISM_INSTRUCTION_HPP_
#define PRISM_INSTRUCTION_HPP_

#include <cstddef>
#include <cstdint>

#include "prism/color.hpp"
#include "prism/strip.hpp"

namespace prism {

/// @brief Millisecond offset from the owning group's activation time.
///
/// The current wire commands retain their existing mark representation; a
/// group's local origin is applied by the controller.
using Mark = std::uint16_t;

/// @brief Label identifying an instruction group.
///
/// Group IDs are labels rather than indexes.  ID 0 is reserved for the
/// controller's implicit root group.
using GroupId = std::uint32_t;

/// @brief Additional passes requested for a group activation.
///
/// A value of zero means one pass.  The all-ones value is reserved for the
/// root start policy; child activations must use a finite value.
using LoopCount = std::uint16_t;

/// @brief Sentinel requesting an unbounded root repeat policy.
constexpr LoopCount kForeverLoopCount = 0xFFFFU;

/// @brief Largest finite additional-repeat count.
constexpr LoopCount kMaxFiniteLoopCount = kForeverLoopCount - 1U;

/// @brief Shared fixed capacity for stored instructions and pending indexes.
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
  /// @brief Start a non-root group timeline.
  kRunGroup,
#if defined(PRISM_CONTROLLER_TESTING)
  /// @brief Host-test-only timed instruction.
  kTestTimed,
#endif
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
/// The ``mark`` field is an offset within the owning group's timeline.
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

/// @brief Controller-side configuration for a RunGroup instruction.
///
/// This is not a serialized protocol payload.  Wire tags and byte layouts are
/// defined by the protocol integration step.
struct RunGroupPayload {
  /// @brief Group whose local timeline schedules this activation.
  GroupId group_id{0U};
  /// @brief Offset within the parent group's local timeline.
  Mark mark{0U};
  /// @brief Nonzero sparse label of the group to activate.
  GroupId target_id{0U};
  /// @brief Finite number of additional child passes.
  LoopCount additional_repeats{0U};
};

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
  ///     milliseconds after which Execute() should be called again. Positive
  ///     durations must be less than half the uint32 timestamp range.
  virtual std::uint32_t Execute() = 0;

  /// @brief Return the tag identifying this instruction's concrete type.
  /// @return InstructionTag value set by the derived-class constructor.
  InstructionTag Tag() const { return tag_; }

  /// @brief Write a human-readable description of this instruction into
  ///     a caller-provided buffer.
  /// @param buf  Destination buffer.
  /// @param size Buffer capacity.
  virtual void ToString(char* buf, std::size_t size) const;

  /// @brief Reset transient execution state before replaying this instruction.
  ///
  /// Configuration copied into the fixed instruction slot remains unchanged.
  /// Stateful instructions override this hook to clear completion or timing
  /// state before a new group activation.
  virtual void ResetForReplay();

  /// @brief Report whether this instruction provides a positive time boundary.
  ///
  /// The controller uses this static capability during pre-run validation of
  /// repeated groups.  Instant instructions return false.
  /// @return True when a future deadline can bound a repeated pass.
  virtual bool HasPositiveSchedulingBoundary() const;

  /// @brief Report whether this instruction starts a child group.
  /// @return True only for RunGroupInstruction.
  virtual bool IsRunGroup() const;

  /// @brief Return the target group label for a RunGroup instruction.
  /// @return Target GroupId, or zero for other instruction types.
  virtual GroupId TargetGroupId() const;

  /// @brief Return the child repeat count for a RunGroup instruction.
  /// @return Additional repeats, or zero for other instruction types.
  virtual LoopCount AdditionalRepeats() const;

 protected:
  ControllerInstruction() = default;
  explicit ControllerInstruction(Mark m) : mark{m} {}
  InstructionTag tag_{};

 public:
  /// @brief Offset from the owning group's local activation time.
  Mark mark{0U};
  /// @brief Non-owning pointer to the target strip, or nullptr.
  Strip* strip{nullptr};
  /// @brief Non-owning pointer to the owning controller, or nullptr.
  ///     Set by Controller::AddInstruction.
  Controller* controller{nullptr};
  /// @brief Group label owning this instruction.  Defaults to the root group.
  GroupId group_id{0U};
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

/// @brief Instruction that activates another group's timeline.
class RunGroupInstruction : public ControllerInstruction {
 public:
  /// @brief Construct an empty root-owned group activation.
  RunGroupInstruction() { tag_ = InstructionTag::kRunGroup; }

  /// @brief Construct from a controller RunGroup payload.
  /// @param payload Source payload to unpack.
  explicit RunGroupInstruction(const RunGroupPayload& payload)
      : ControllerInstruction(payload.mark),
        target_id(payload.target_id),
        additional_repeats(payload.additional_repeats) {
    tag_ = InstructionTag::kRunGroup;
    group_id = payload.group_id;
  }

  /// @brief Direct execution is a no-op; Controller::Run handles activation.
  /// @return 0 when called outside the controller's group scheduler.
  std::uint32_t Execute() override;

  /// @brief Write a description including target and repeat policy.
  /// @param buf Destination buffer.
  /// @param size Buffer capacity.
  void ToString(char* buf, std::size_t size) const override;

  /// @brief Identify this instruction as a child-group activation.
  /// @return Always true.
  bool IsRunGroup() const override;

  /// @brief Return the requested child group label.
  /// @return Target GroupId.
  GroupId TargetGroupId() const override;

  /// @brief Return the requested additional child repeats.
  /// @return LoopCount repeat count.
  LoopCount AdditionalRepeats() const override;

  /// @brief Nonzero group label to activate.
  GroupId target_id{0U};
  /// @brief Finite additional child passes.
  LoopCount additional_repeats{0U};
};

#if defined(PRISM_CONTROLLER_TESTING)
namespace test {

/// @brief Fixed-storage timed instruction available only to host tests.
class TimedInstruction : public ControllerInstruction {
 public:
  /// @brief Construct a one-yield instruction for controller timing tests.
  /// @param delay_ms Duration returned on the first execution.
  /// @param execution_count Optional non-owning counter; it must outlive every
  ///     stored copy of this instruction.
  /// @param yield_on_first_execution Whether Execute() yields its advertised
  ///     positive boundary on the first call.
  TimedInstruction(std::uint32_t delay_ms, std::uint32_t* execution_count,
                   bool yield_on_first_execution = true);

  /// @brief Yield once, then complete on the next execution.
  /// @return The configured delay on the first call, then zero.
  std::uint32_t Execute() override;

  /// @brief Write a description for controller debug logs.
  /// @param buf Destination buffer.
  /// @param size Buffer capacity.
  void ToString(char* buf, std::size_t size) const override;

  /// @brief Reset the yield state before a group pass is replayed.
  void ResetForReplay() override;

  /// @brief Report whether the configured delay provides a repeat boundary.
  /// @return True when the configured delay is positive.
  bool HasPositiveSchedulingBoundary() const override;

 private:
  std::uint32_t delay_ms_{0U};
  std::uint32_t* execution_count_{nullptr};
  bool yield_on_first_execution_{true};
  bool yielded_{false};
};

}  // namespace test
#endif

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
    /// @brief Active member: child-group activation instruction.
    RunGroupInstruction run_group;
#if defined(PRISM_CONTROLLER_TESTING)
    /// @brief Active member: host-test timed instruction.
    test::TimedInstruction test_timed;
#endif
  };

  /// @brief Tag identifying the currently-active member.
  InstructionTag tag{};

  /// @brief Default constructor — leaves storage uninitialised and inactive.
  InstructionMemorySlot() : tag{}, active_(false) {}

  /// @brief Destroy the active member before the slot goes out of scope.
  ~InstructionMemorySlot();

  InstructionMemorySlot(const InstructionMemorySlot&) = delete;
  InstructionMemorySlot& operator=(const InstructionMemorySlot&) = delete;

  /// @brief Activate this slot with a copy of an already-constructed
  ///     instruction.  The previously-active member is destroyed first.
  /// @param instr Pointer to the source instruction.  Must not be null.
  /// @return True when the instruction tag was stored.
  bool Set(const ControllerInstruction* instr);

  /// @brief Destroy the active instruction and leave the slot empty.
  void Clear();

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

  /// @brief Return the owning group label.
  /// @return GroupId, or zero when the slot is empty.
  GroupId GetGroupId() const;

  /// @brief Return whether the active instruction is a RunGroup.
  /// @return True for a child-group activation.
  bool IsRunGroup() const;

  /// @brief Return the RunGroup target label.
  /// @return Target GroupId, or zero for other instructions.
  GroupId GetTargetGroupId() const;

  /// @brief Return the RunGroup repeat count.
  /// @return LoopCount, or zero for other instructions.
  LoopCount GetAdditionalRepeats() const;

  /// @brief Return whether this slot offers a positive static time boundary.
  /// @return True when the active instruction can bound a repeated pass.
  bool HasPositiveSchedulingBoundary() const;

  /// @brief Reset transient state in the active instruction for replay.
  void ResetForReplay();

 private:
  /// @brief Return a base-class pointer to the active instruction.
  ControllerInstruction* Active();

  /// @brief Return a const base-class pointer to the active instruction.
  const ControllerInstruction* Active() const;

  /// @brief Destroy the active member and reset the tag.
  void Destroy();

  /// @brief True once a concrete union member has been constructed.
  bool active_;
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

}  // namespace prism

#endif /* PRISM_INSTRUCTION_HPP_ */
