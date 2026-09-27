#ifndef PRISM_CONTROLLER_HPP_
#define PRISM_CONTROLLER_HPP_

#include <cstdint>

#include "prism/debug.hpp"
#include "prism/instruction.hpp"
#include "prism/strip.hpp"

namespace prism {

/// @brief Callback returning a 32-bit timestamp in milliseconds.
/// @return Current counter value; active intervals must stay below half range.
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
  /// @brief Program preflight failed or a repeat-boundary invariant was
  ///     violated at runtime.
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
/// The controller resets this record for each pass while retaining its
/// immutable group metadata.
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
  /// @brief Whether this pass observed a positive scheduling boundary.
  bool positive_boundary_observed{false};
  /// @brief Parent runtime index, or kMaxGroups for the root.
  std::uint32_t parent_group_index{kMaxGroups};
  /// @brief Nesting depth used to advance children before their parents.
  std::uint8_t depth{0U};
  /// @brief Whether this runtime currently represents an active invocation.
  bool active{false};

  /// @brief Clear transient activation state while retaining group metadata.
  /// @return None.
  void Reset();
};

/// @brief High-level animation controller for a Prism Kit strip.
///
/// Accepts fixed-capacity group programs and advances their timelines when
/// Run() is called.
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

  /// @brief Initialize or replay the retained program from the root group.
  /// @param root_additional_repeats Additional root passes; the forever
  ///     sentinel is permitted only here.
  /// @return Success, busy, capacity, or invalid-program status. No
  ///     instruction executes until the caller invokes Run().
  ControllerStatus Start(LoopCount root_additional_repeats = 0U);

  /// @brief Clear all queued instructions and abort active runtimes.
  void ResetInstructions();

  /// @brief Advance active group timelines and schedule the next progress tick.
  ///
  /// Calls while idle or completed are harmless no-ops; Run() never starts an
  /// editable program implicitly.
  /// @pre A valid timestamp callback must be registered before an active run.
  /// @return Success or the first runtime group-execution failure this tick.
  ControllerStatus Run();

  /// @brief Register a timestamp callback for timing-aware execution.
  /// @param callback Function returning the current timestamp in milliseconds.
  void SetTimestampCallback(TimestampCallback callback);

  /// @brief Query the current 32-bit timestamp.
  /// @return Counter value in milliseconds, wrapping at uint32 overflow, or 0
  ///     if no callback is registered.
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

  /// @brief Fixed bookkeeping for one timed instruction under execution.
  struct ExecutingInstruction {
    /// @brief Index of the instruction slot being resumed.
    std::uint32_t instruction_index{0U};
    /// @brief Index of the owning group runtime.
    std::uint32_t runtime_index{0U};
    /// @brief Absolute timestamp at which the instruction is next ready.
    std::uint32_t deadline{0U};
  };

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

  /// @brief Reset replay state for all instructions in one group.
  /// @param runtime_index Index into the validated runtime table.
  void ResetGroupInstructionReplayState(std::uint32_t runtime_index);

  /// @brief Record a positive boundary for a runtime and its active ancestors.
  /// @param runtime_index Group runtime where the boundary was observed.
  void ObservePositiveBoundary(std::uint32_t runtime_index);

  /// @brief Initialize one group invocation and reset its instruction state.
  /// @param group_index Group runtime to activate.
  /// @param timestamp Local-time origin for this invocation.
  /// @param additional_repeats Number of passes after the first.
  /// @param parent_group_index Parent runtime, or kMaxGroups for the root.
  /// @return True when the runtime index is valid and activation was recorded.
  bool StartGroupRuntime(std::uint32_t group_index, std::uint32_t timestamp,
                         LoopCount additional_repeats,
                         std::uint32_t parent_group_index);

  /// @brief Resume timed instructions whose deadlines have arrived.
  /// @param timestamp Timestamp shared by the current progress tick.
  /// @param run_status First failure observed during this tick.
  /// @return True if any timed instruction was resumed or removed.
  bool DrainExecuting(std::uint32_t timestamp, ControllerStatus& run_status);

  /// @brief Complete or repeat groups with no outstanding work.
  /// @param timestamp Timestamp shared by the current progress tick.
  /// @param run_status First failure observed during this tick.
  /// @return True if any group completion or repeat transition occurred.
  bool CompleteReadyGroups(std::uint32_t timestamp,
                           ControllerStatus& run_status);

  /// @brief Execute a due instruction or activate its child group.
  /// @param runtime_index Index of the owning group runtime.
  /// @param slot_index Index of the due instruction slot.
  /// @param timestamp Timestamp shared by the current progress tick.
  /// @param run_status First failure observed during this tick.
  /// @return True only when a child starts and should run before its parent.
  bool DispatchDueInstruction(std::uint32_t runtime_index,
                              std::uint32_t slot_index, std::uint32_t timestamp,
                              ControllerStatus& run_status);

  /// @brief Execute newly due instructions, prioritizing deeper child groups.
  /// @param timestamp Timestamp shared by the current progress tick.
  /// @param run_status First failure observed during this tick.
  /// @return True if any instruction was consumed or a child was activated.
  bool PickDueInstructions(std::uint32_t timestamp,
                           ControllerStatus& run_status);

  /// @brief Schedule the earliest mark or timed-instruction deadline.
  /// @param timestamp Timestamp shared by the current progress tick.
  void ScheduleNextRun(std::uint32_t timestamp);

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

  /// @brief Log a due-instruction step with mark and group-local elapsed time.
  /// @param idx     Index into instructions_[] for the instruction being
  /// picked.
  /// @param elapsed Milliseconds since the owning group's activation.
  void DebugLogPick(std::uint32_t idx, std::uint32_t elapsed) const;

  /// @brief Non-owning pointer to the bound strip, or nullptr.
  Strip* strip_;
  /// @brief Fixed-capacity instruction queue.
  InstructionMemorySlot instructions_[kMaxInstruction];
  /// @brief Number of active instructions in the queue.
  std::uint32_t instruction_count_;
  /// @brief Timed instructions and their group-relative completion tracking.
  ExecutingInstruction executing_[kMaxExecuting]{};
  /// @brief Number of entries in executing_.
  std::uint32_t executing_count_{0U};

  /// @brief Set by instructions via RequestShow(); cleared at each Run().
  bool show_requested_{false};
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
  /// @brief Fixed shared instruction indexes grouped and sorted by mark.
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
