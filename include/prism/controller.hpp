#ifndef PRISM_CONTROLLER_HPP_
#define PRISM_CONTROLLER_HPP_

#include <cstdint>

#include "prism/debug.hpp"
#include "prism/instruction.hpp"
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

/// @brief Common status returned by controller mutations and progress ticks.
enum class ControllerStatus : std::uint8_t {
  /// @brief The requested operation completed successfully.
  kSuccess = 0U,
  /// @brief Alias for callers that prefer the shorter success name.
  kOk = kSuccess,
  /// @brief An argument or instruction pointer was not valid.
  kInvalidArgument,
  /// @brief The retained program is active or locked after completion.
  kBusy,
  /// @brief Alias describing the completed-program lock explicitly.
  kProgramLocked = kBusy,
  /// @brief The fixed instruction or group capacity was exhausted.
  kCapacityExhausted,
  /// @brief Alias for the capacity failure.
  kCapacity = kCapacityExhausted,
  /// @brief The retained program failed pre-run validation.
  kInvalidProgram,
  /// @brief A requested child group is already active.
  kAlreadyActiveGroup,
  /// @brief Alias for the active-group failure.
  kGroupAlreadyActive = kAlreadyActiveGroup,
};

/// @brief Maximum number of group records, including the implicit root.
constexpr std::uint32_t kMaxGroups = kMaxInstructions + 1U;

/// @brief Immutable metadata describing one compact group view.
struct GroupRecord {
  /// @brief Sparse label identifying the group.
  GroupId id{0U};
  /// @brief Offset into the controller's shared instruction-index pool.
  std::uint32_t index_begin{0U};
  /// @brief Number of instruction indexes belonging to this group.
  std::uint32_t instruction_count{0U};
};

/// @brief Fixed runtime storage reserved for one group activation.
///
/// Step 1 initializes and resets this record; nested scheduling consumes the
/// cursor, origin, repeat, and outstanding-work fields in the later runtime
/// step.
struct GroupRuntime {
  /// @brief Immutable group metadata copied at validation time.
  GroupRecord record{};
  /// @brief Next instruction position within record.
  std::uint32_t cursor{0U};
  /// @brief Local timestamp origin for the current pass.
  std::uint32_t start_time{0U};
  /// @brief Additional passes remaining after the current pass.
  LoopCount repeats_remaining{0U};
  /// @brief Number of timed instructions still outstanding.
  std::uint32_t active_count{0U};
  /// @brief Number of child activations still outstanding.
  std::uint32_t child_count{0U};
  /// @brief Whether this runtime currently represents an active invocation.
  bool active{false};

  /// @brief Clear transient activation state while retaining group metadata.
  /// @return None.
  void Reset();
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
  /// @brief Maximum number of compact group records.
  static constexpr std::uint32_t kMaxGroups = prism::kMaxGroups;
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
  /// @return Success, invalid argument, busy, or capacity-exhausted status.
  ControllerStatus AddInstruction(const ControllerInstruction* instr);

  /// @brief Start or replay the retained program from the root group.
  /// @param root_additional_repeats Additional root passes; the forever
  ///     sentinel is permitted only here.
  /// @return Success, busy, capacity, or invalid-program status.
  ControllerStatus Start(LoopCount root_additional_repeats = 0U);

  /// @brief Clear all queued instructions and abort active runtimes.
  void ResetInstructions();

  /// @brief Iterate through instructions: drain executing array first, then
  ///     pick new instructions whose mark <= current timestamp.
  ///
  /// @pre A valid timestamp callback must be registered before calling Run().
  /// @return Success or a controller/program-validation failure.
  ControllerStatus Run();

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
  /// @brief Fixed scratch storage used while constructing a validated model.
  struct GroupBuildState;

  /// @brief Lifecycle state of the retained instruction program.
  enum class ProgramState : std::uint8_t {
    kEditable,
    kActive,
    kCompleted,
  };

  /// @brief Build group views and validate the retained program.
  /// @param root_additional_repeats Root repeat policy being started.
  /// @return Success when metadata can be committed.
  ControllerStatus BuildAndValidateGroups(LoopCount root_additional_repeats);

  /// @brief Count stored instructions into sparse group records.
  /// @param build Scratch state receiving the implicit root and group counts.
  /// @return Success, or capacity-exhausted if the group bound is exceeded.
  ControllerStatus BuildGroupTable(GroupBuildState& build) const;

  /// @brief Build stable shared-index ranges for the group table.
  /// @param build Scratch state containing group counts and receiving indexes.
  /// @return Success, or capacity-exhausted if the instruction bound is
  /// exceeded.
  ControllerStatus BuildGroupIndexPool(GroupBuildState& build) const;

  /// @brief Validate RunGroup targets and construct the bounded call graph.
  /// @param build Scratch state containing groups and receiving graph edges.
  /// @return Success, or invalid-program when a target or repeat is invalid.
  ControllerStatus BuildGroupCallGraph(GroupBuildState& build) const;

  /// @brief Reject call-graph cycles and compute transitive reachability.
  /// @param build Scratch state whose graph is checked and closed in place.
  /// @return Success, or invalid-program if any cycle exists.
  ControllerStatus ValidateGroupCallGraph(GroupBuildState& build) const;

  /// @brief Validate child and root repeat policies against positive
  /// boundaries.
  /// @param build Validated scratch state with group reachability information.
  /// @param root_additional_repeats Root repeat policy requested by Start().
  /// @return Success, or invalid-program if a repeated group has no boundary.
  ControllerStatus ValidateRepeatBoundaries(
    GroupBuildState& build, LoopCount root_additional_repeats) const;

  /// @brief Publish a validated scratch model to fixed controller storage.
  /// @param build Complete group records and shared instruction indexes.
  void CommitGroupBuild(const GroupBuildState& build);

  /// @brief Find a group label in a bounded group table.
  /// @param groups Candidate group table.
  /// @param group_count Number of valid records in groups.
  /// @param id Sparse label to find.
  /// @return Table index, or kMaxGroups when absent.
  static std::uint32_t FindGroup(const GroupRecord* groups,
                                 std::uint32_t group_count, GroupId id);

  /// @brief Reset all active slots before a new program activation.
  void ResetInstructionReplayState();

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
  /// @brief Current editable, active, or completed program state.
  ProgramState state_{ProgramState::kEditable};
  /// @brief Root repeat policy retained for the active invocation.
  LoopCount root_additional_repeats_{0U};
  /// @brief Fixed shared instruction indexes grouped by GroupRecord.
  std::uint32_t group_indices_[kMaxInstructions]{};
  /// @brief Compact group metadata built at Start().
  GroupRecord groups_[kMaxGroups]{};
  /// @brief Fixed runtime records reserved for every possible group.
  GroupRuntime runtimes_[kMaxGroups]{};
  /// @brief Number of valid records in groups_.
  std::uint32_t group_count_{0U};
};

}  // namespace prism

#endif /* PRISM_CONTROLLER_HPP_ */
