#include "prism/controller.hpp"

#include <cstdarg>
#include <cstdint>
#include <cstdio>

#include "prism/debug.hpp"
#include "prism/instruction.hpp"
#include "prism/strip.hpp"

static_assert(prism::kMaxGroups <= 32U,
              "Group graph masks require at most 32 groups");

namespace {

constexpr std::uint32_t kTimestampHalfRange = 0x80000000U;

bool DeadlineReached(std::uint32_t timestamp, std::uint32_t deadline) {
  return timestamp - deadline < kTimestampHalfRange;
}

void LatchRunStatus(prism::ControllerStatus& run_status,
                    prism::ControllerStatus status) {
  if (run_status == prism::ControllerStatus::kSuccess &&
      status != prism::ControllerStatus::kSuccess) {
    run_status = status;
  }
}

}  // namespace

struct prism::Controller::GroupBuildState {
  GroupRecord groups[Controller::kMaxGroups]{};
  std::uint32_t instruction_indices[prism::kMaxInstructions]{};
  std::uint32_t index_write_positions[Controller::kMaxGroups]{};
  std::uint32_t call_graph[Controller::kMaxGroups]{};
  bool has_positive_boundary[Controller::kMaxGroups]{};
  std::uint32_t group_count{1U};
};

// ====================================================================
// Controller
// ====================================================================

prism::Controller::Controller()
    : strip_(nullptr),
      instruction_count_(0U),
      executing_count_(0U),
      get_timestamp_(nullptr),
      schedule_next_run_(nullptr),
      debug_(nullptr) {}

std::uint32_t prism::Controller::FindGroup(const GroupRecord* groups,
                                           std::uint32_t group_count,
                                           GroupId id) {
  for (std::uint32_t i = 0U; i < group_count; ++i) {
    if (groups[i].id == id) {
      return i;
    }
  }
  return kMaxGroups;
}

prism::ControllerStatus prism::Controller::BuildAndValidateGroups(
  LoopCount root_additional_repeats) {
  GroupBuildState build{};
  ControllerStatus status = BuildGroupTable(build);
  if (status != ControllerStatus::kSuccess) {
    return status;
  }
  status = BuildGroupIndexPool(build);
  if (status != ControllerStatus::kSuccess) {
    return status;
  }
  status = BuildGroupCallGraph(build);
  if (status != ControllerStatus::kSuccess) {
    return status;
  }
  status = ValidateGroupCallGraph(build);
  if (status != ControllerStatus::kSuccess) {
    return status;
  }
  status = ValidateRepeatBoundaries(build, root_additional_repeats);
  if (status != ControllerStatus::kSuccess) {
    return status;
  }

  CommitGroupBuild(build);
  return ControllerStatus::kSuccess;
}

prism::ControllerStatus prism::Controller::BuildGroupTable(
  GroupBuildState& build) const {
  // Group zero is implicit even when the loaded program has no root
  // instructions.
  build.groups[0].id = 0U;
  for (std::uint32_t i = 0U; i < instruction_count_; ++i) {
    const GroupId group_id = instructions_[i].GetGroupId();
    std::uint32_t group_index =
      FindGroup(build.groups, build.group_count, group_id);
    if (group_index == kMaxGroups) {
      if (build.group_count >= kMaxGroups) {
        return ControllerStatus::kCapacityExhausted;
      }
      group_index = build.group_count++;
      build.groups[group_index].id = group_id;
    }
    ++build.groups[group_index].instruction_count;
  }
  return ControllerStatus::kSuccess;
}

prism::ControllerStatus prism::Controller::BuildGroupIndexPool(
  GroupBuildState& build) const {
  // Prefix the shared index pool with each group's compact range.
  std::uint32_t index_cursor = 0U;
  for (std::uint32_t i = 0U; i < build.group_count; ++i) {
    build.groups[i].index_begin = index_cursor;
    build.index_write_positions[i] = index_cursor;
    index_cursor += build.groups[i].instruction_count;
    if (index_cursor > kMaxInstructions) {
      return ControllerStatus::kCapacityExhausted;
    }
  }

  // Group ranges retain insertion order before a stable mark sort below.
  for (std::uint32_t i = 0U; i < instruction_count_; ++i) {
    const std::uint32_t group_index =
      FindGroup(build.groups, build.group_count, instructions_[i].GetGroupId());
    build.instruction_indices[build.index_write_positions[group_index]++] = i;
  }

  // Each runtime advances one cursor, so keep its compact range mark-sorted.
  // Strict comparison preserves insertion order when marks are equal.
  for (std::uint32_t group_index = 0U; group_index < build.group_count;
       ++group_index) {
    const GroupRecord& group = build.groups[group_index];
    const std::uint32_t end = group.index_begin + group.instruction_count;
    for (std::uint32_t i = group.index_begin + 1U; i < end; ++i) {
      const std::uint32_t instruction_index = build.instruction_indices[i];
      const Mark mark = instructions_[instruction_index].GetMark();
      std::uint32_t insert = i;
      while (insert > group.index_begin &&
             instructions_[build.instruction_indices[insert - 1U]].GetMark() >
               mark) {
        build.instruction_indices[insert] =
          build.instruction_indices[insert - 1U];
        --insert;
      }
      build.instruction_indices[insert] = instruction_index;
    }
  }
  return ControllerStatus::kSuccess;
}

prism::ControllerStatus prism::Controller::BuildGroupCallGraph(
  GroupBuildState& build) const {
  // Resolve every RunGroup target by label and build the bounded call graph.
  for (std::uint32_t i = 0U; i < instruction_count_; ++i) {
    const InstructionMemorySlot& slot = instructions_[i];
    if (!slot.IsRunGroup()) {
      continue;
    }
    const GroupId target_id = slot.GetTargetGroupId();
    if (target_id == 0U || slot.GetAdditionalRepeats() == kForeverLoopCount) {
      return ControllerStatus::kInvalidProgram;
    }
    const std::uint32_t source_index =
      FindGroup(build.groups, build.group_count, slot.GetGroupId());
    const std::uint32_t target_index =
      FindGroup(build.groups, build.group_count, target_id);
    if (source_index == kMaxGroups || target_index == kMaxGroups) {
      return ControllerStatus::kInvalidProgram;
    }
    build.call_graph[source_index] |= (1U << target_index);
  }
  return ControllerStatus::kSuccess;
}

prism::ControllerStatus prism::Controller::ValidateGroupCallGraph(
  GroupBuildState& build) const {
  // Transitive closure is bounded by kMaxGroups and rejects every cycle,
  // including a direct self-call, before any instruction is executed.  The
  // current group limit fits in one 32-bit adjacency mask per group.
  for (std::uint32_t through = 0U; through < build.group_count; ++through) {
    for (std::uint32_t source = 0U; source < build.group_count; ++source) {
      if ((build.call_graph[source] & (1U << through)) == 0U) {
        continue;
      }
      build.call_graph[source] |= build.call_graph[through];
    }
  }
  for (std::uint32_t i = 0U; i < build.group_count; ++i) {
    if ((build.call_graph[i] & (1U << i)) != 0U) {
      return ControllerStatus::kInvalidProgram;
    }
  }
  return ControllerStatus::kSuccess;
}

prism::ControllerStatus prism::Controller::ValidateRepeatBoundaries(
  GroupBuildState& build, LoopCount root_additional_repeats) const {
  // Marks and statically-declared timed work are positive boundaries for the
  // owning group.  Child boundaries propagate back over the acyclic graph.
  for (std::uint32_t i = 0U; i < instruction_count_; ++i) {
    const InstructionMemorySlot& slot = instructions_[i];
    const std::uint32_t group_index =
      FindGroup(build.groups, build.group_count, slot.GetGroupId());
    if (slot.GetMark() > 0U || slot.HasPositiveSchedulingBoundary()) {
      build.has_positive_boundary[group_index] = true;
    }
  }
  for (std::uint32_t pass = 0U; pass < build.group_count; ++pass) {
    bool changed = false;
    for (std::uint32_t source = 0U; source < build.group_count; ++source) {
      for (std::uint32_t target = 0U; target < build.group_count; ++target) {
        if ((build.call_graph[source] & (1U << target)) != 0U &&
            build.has_positive_boundary[target] &&
            !build.has_positive_boundary[source]) {
          build.has_positive_boundary[source] = true;
          changed = true;
        }
      }
    }
    if (!changed) {
      break;
    }
  }

  // Every repeated child must have a positive completion boundary.  The same
  // rule applies to a repeated root, including the forever sentinel.
  for (std::uint32_t i = 0U; i < instruction_count_; ++i) {
    const InstructionMemorySlot& slot = instructions_[i];
    if (!slot.IsRunGroup() || slot.GetAdditionalRepeats() == 0U) {
      continue;
    }
    const std::uint32_t target_index =
      FindGroup(build.groups, build.group_count, slot.GetTargetGroupId());
    if (target_index == kMaxGroups ||
        !build.has_positive_boundary[target_index]) {
      return ControllerStatus::kInvalidProgram;
    }
  }
  if (root_additional_repeats != 0U && !build.has_positive_boundary[0U]) {
    return ControllerStatus::kInvalidProgram;
  }
  return ControllerStatus::kSuccess;
}

void prism::Controller::CommitGroupBuild(const GroupBuildState& build) {
  // Publish only a completely validated model so a failed start leaves the
  // retained instruction program and its previous metadata untouched.
  group_count_ = build.group_count;
  for (std::uint32_t i = 0U; i < kMaxGroups; ++i) {
    groups_[i] = (i < build.group_count) ? build.groups[i] : GroupRecord{};
    runtimes_[i] = GroupRuntime{};
  }
  for (std::uint32_t i = 0U; i < kMaxInstructions; ++i) {
    group_indices_[i] = build.instruction_indices[i];
  }
  for (std::uint32_t i = 0U; i < build.group_count; ++i) {
    runtimes_[i].record = groups_[i];
  }
}

void prism::Controller::ResetGroupInstructionReplayState(
  std::uint32_t runtime_index) {
  const GroupRecord& record = runtimes_[runtime_index].record;
  for (std::uint32_t i = 0U; i < record.instruction_count; ++i) {
    instructions_[group_indices_[record.index_begin + i]].ResetForReplay();
  }
}

void prism::Controller::ObservePositiveBoundary(std::uint32_t runtime_index) {
  for (std::uint32_t depth = 0U;
       depth < group_count_ && runtime_index < group_count_; ++depth) {
    GroupRuntime& runtime = runtimes_[runtime_index];
    if (!runtime.active) {
      return;
    }
    runtime.positive_boundary_observed = true;
    runtime_index = runtime.parent_group_index;
  }
}

bool prism::Controller::StartGroupRuntime(std::uint32_t group_index,
                                          std::uint32_t timestamp,
                                          LoopCount additional_repeats,
                                          std::uint32_t parent_group_index) {
  if (group_index >= group_count_ ||
      (parent_group_index != kMaxGroups &&
       (parent_group_index >= group_count_ ||
        !runtimes_[parent_group_index].active))) {
    return false;
  }
  GroupRuntime& runtime = runtimes_[group_index];
  runtime.Reset();
  runtime.start_time = timestamp;
  runtime.repeats_remaining = additional_repeats;
  runtime.parent_group_index = parent_group_index;
  if (parent_group_index == kMaxGroups) {
    runtime.depth = 0U;
  } else {
    runtime.depth =
      static_cast<std::uint8_t>(runtimes_[parent_group_index].depth + 1U);
  }
  runtime.active = true;
  ResetGroupInstructionReplayState(group_index);
  return true;
}

void prism::Controller::SetStrip(Strip* strip) {
  DebugLog("SetStrip(%p, name=\"%s\")", reinterpret_cast<void*>(strip),
           strip != nullptr ? strip->Name() : "null");
  strip_ = strip;
}

void prism::Controller::SetTimestampCallback(TimestampCallback callback) {
  DebugLog("SetTimestampCallback(%p)", reinterpret_cast<void*>(callback));
  get_timestamp_ = callback;
}

void prism::Controller::SetScheduleCallback(ScheduleCallback callback) {
  DebugLog("SetScheduleCallback(%p)", reinterpret_cast<void*>(callback));
  schedule_next_run_ = callback;
}

void prism::Controller::SetDebug(Debug* d) {
  debug_ = d;
  DebugLog("SetDebug(%p)", reinterpret_cast<void*>(d));
}

void prism::Controller::RequestShow() {
  DebugLog("RequestShow");
  show_requested_ = true;
}

std::uint32_t prism::Controller::GetTimestamp() const {
  const std::uint32_t ts = (get_timestamp_ != nullptr) ? get_timestamp_() : 0U;
  DebugLog("GetTimestamp => %u", ts);
  return ts;
}

void prism::Controller::ScheduleTimeout(std::uint32_t ms) {
  DebugLog("ScheduleTimeout(%u)", ms);
  if (min_scheduled_timeout_ == 0U || ms < min_scheduled_timeout_) {
    min_scheduled_timeout_ = ms;
  }
}

prism::ControllerStatus prism::Controller::AddInstruction(
  const ControllerInstruction* instr) {
  if (instr == nullptr) {
    DebugLog("AddInstruction rejected null instruction");
    return ControllerStatus::kInvalidArgument;
  }
  if (state_ != ProgramState::kEditable) {
    DebugLog("AddInstruction rejected locked program");
    return ControllerStatus::kBusy;
  }
  if (instruction_count_ >= kMaxInstruction) {
    DebugLog("AddInstruction queue full (%u)", kMaxInstruction);
    return ControllerStatus::kCapacityExhausted;
  }
  DebugLog("AddInstruction tag=%hhu mark=%u",
           static_cast<std::uint8_t>(instr->Tag()),
           static_cast<unsigned>(instr->mark));

  // Store the instruction at the next free slot.
  const std::uint32_t slot_idx = instruction_count_;
  auto& slot = instructions_[slot_idx];
  if (!slot.Set(instr)) {
    DebugLog("AddInstruction rejected unsupported tag");
    return ControllerStatus::kInvalidArgument;
  }
  slot.SetStrip(strip_);
  slot.SetController(this);
  ++instruction_count_;

  return ControllerStatus::kSuccess;
}

prism::ControllerStatus prism::Controller::Start(
  LoopCount root_additional_repeats) {
  if (state_ == ProgramState::kActive) {
    DebugLog("Start rejected while active");
    return ControllerStatus::kBusy;
  }
  const ControllerStatus status =
    BuildAndValidateGroups(root_additional_repeats);
  if (status != ControllerStatus::kSuccess) {
    DebugLog("Start rejected status=%u", static_cast<unsigned>(status));
    return status;
  }

  executing_count_ = 0U;
  show_requested_ = false;
  min_scheduled_timeout_ = 0U;
  const std::uint32_t root_index =
    FindGroup(groups_, group_count_, GroupId{0U});
  if (root_index == kMaxGroups) {
    return ControllerStatus::kInvalidProgram;
  }
  const std::uint32_t timestamp = GetTimestamp();
  if (groups_[root_index].instruction_count == 0U) {
    runtimes_[root_index].Reset();
    state_ = ProgramState::kCompleted;
    return ControllerStatus::kSuccess;
  }
  if (!StartGroupRuntime(root_index, timestamp, root_additional_repeats,
                         kMaxGroups)) {
    return ControllerStatus::kInvalidProgram;
  }
  state_ = ProgramState::kActive;
  return ControllerStatus::kSuccess;
}

void prism::Controller::ResetInstructions() {
  DebugLog("ResetInstructions");
  for (std::uint32_t i = 0U; i < kMaxInstruction; ++i) {
    instructions_[i].Clear();
  }
  instruction_count_ = 0U;
  executing_count_ = 0U;
  state_ = ProgramState::kEditable;
  show_requested_ = false;
  min_scheduled_timeout_ = 0U;
  group_count_ = 0U;
  for (std::uint32_t i = 0U; i < kMaxGroups; ++i) {
    groups_[i] = GroupRecord{};
    runtimes_[i] = GroupRuntime{};
  }
  for (std::uint32_t i = 0U; i < kMaxInstructions; ++i) {
    group_indices_[i] = 0U;
  }
}

prism::ControllerStatus prism::Controller::Run() {
  if (state_ != ProgramState::kActive) {
    return ControllerStatus::kSuccess;
  }

  const std::uint32_t timestamp = GetTimestamp();
  DebugLog("Run");
  show_requested_ = false;
  min_scheduled_timeout_ = 0U;

  ControllerStatus run_status = ControllerStatus::kSuccess;
  bool progressed = false;
  do {
    progressed = DrainExecuting(timestamp, run_status);
    progressed = CompleteReadyGroups(timestamp, run_status) || progressed;
    progressed = PickDueInstructions(timestamp, run_status) || progressed;
  } while (progressed);

  ScheduleNextRun(timestamp);
  if (show_requested_ && strip_ != nullptr) {
    DebugLog("strip->Show()");
    strip_->Show();
  }

  return run_status;
}

bool prism::Controller::DrainExecuting(std::uint32_t timestamp,
                                       ControllerStatus& run_status) {
  bool progressed = false;
  std::uint32_t i = 0U;
  while (i < executing_count_) {
    const ExecutingInstruction instruction = executing_[i];
    if (instruction.instruction_index >= instruction_count_ ||
        instruction.runtime_index >= group_count_ ||
        !runtimes_[instruction.runtime_index].active) {
      DebugLog("Discarding timed instruction with invalid runtime");
      LatchRunStatus(run_status, ControllerStatus::kInvalidProgram);
      --executing_count_;
      executing_[i] = executing_[executing_count_];
      progressed = true;
      continue;
    }
    if (!DeadlineReached(timestamp, instruction.deadline)) {
      ++i;
      continue;
    }

    ObservePositiveBoundary(instruction.runtime_index);
    DebugLogDrain(i, instruction.instruction_index);
    const std::uint32_t delay =
      instructions_[instruction.instruction_index].Execute();
    progressed = true;
    if (delay != 0U) {
      executing_[i].deadline = timestamp + delay;
      ++i;
      continue;
    }

    GroupRuntime& runtime = runtimes_[instruction.runtime_index];
    if (runtime.active_count == 0U) {
      DebugLog("Timed instruction completion count underflow");
      LatchRunStatus(run_status, ControllerStatus::kInvalidProgram);
    } else {
      --runtime.active_count;
    }
    --executing_count_;
    executing_[i] = executing_[executing_count_];
  }
  return progressed;
}

bool prism::Controller::CompleteReadyGroups(std::uint32_t timestamp,
                                            ControllerStatus& run_status) {
  bool progressed = false;
  for (std::uint32_t i = 0U; i < group_count_; ++i) {
    GroupRuntime& runtime = runtimes_[i];
    if (!runtime.active || runtime.cursor < runtime.record.instruction_count ||
        runtime.active_count != 0U || runtime.child_count != 0U) {
      continue;
    }

    progressed = true;
    const LoopCount repeats_remaining = runtime.repeats_remaining;
    const std::uint32_t parent_group_index = runtime.parent_group_index;
    if (repeats_remaining != 0U && runtime.positive_boundary_observed) {
      const LoopCount next_repeats =
        repeats_remaining == kForeverLoopCount
          ? kForeverLoopCount
          : static_cast<LoopCount>(repeats_remaining - 1U);
      if (!StartGroupRuntime(i, timestamp, next_repeats, parent_group_index)) {
        DebugLog("Unable to restart group runtime=%u", i);
        LatchRunStatus(run_status, ControllerStatus::kInvalidProgram);
        runtime.Reset();
      }
      continue;
    }
    if (repeats_remaining != 0U) {
      DebugLog("Repeated group %u completed without observing a boundary", i);
      LatchRunStatus(run_status, ControllerStatus::kInvalidProgram);
    }

    runtime.Reset();
    if (i == 0U) {
      state_ = ProgramState::kCompleted;
      continue;
    }
    if (parent_group_index >= group_count_ ||
        !runtimes_[parent_group_index].active ||
        runtimes_[parent_group_index].child_count == 0U) {
      DebugLog("Group completion has no active parent runtime=%u", i);
      LatchRunStatus(run_status, ControllerStatus::kInvalidProgram);
      continue;
    }
    --runtimes_[parent_group_index].child_count;
  }
  return progressed;
}

bool prism::Controller::DispatchDueInstruction(std::uint32_t runtime_index,
                                               std::uint32_t slot_index,
                                               std::uint32_t timestamp,
                                               ControllerStatus& run_status) {
  if (instructions_[slot_index].IsRunGroup()) {
    const GroupId target_id = instructions_[slot_index].GetTargetGroupId();
    const std::uint32_t target_index =
      FindGroup(groups_, group_count_, target_id);
    if (target_index == kMaxGroups || target_index == 0U) {
      DebugLog("RunGroup target %u is invalid",
               static_cast<unsigned>(target_id));
      LatchRunStatus(run_status, ControllerStatus::kInvalidProgram);
      return false;
    }
    if (runtimes_[target_index].active) {
      DebugLog("RunGroup target %u is already active",
               static_cast<unsigned>(target_id));
      LatchRunStatus(run_status, ControllerStatus::kAlreadyActiveGroup);
      return false;
    }
    if (!StartGroupRuntime(target_index, timestamp,
                           instructions_[slot_index].GetAdditionalRepeats(),
                           runtime_index)) {
      DebugLog("Unable to start group runtime=%u", target_index);
      LatchRunStatus(run_status, ControllerStatus::kInvalidProgram);
      return false;
    }
    ++runtimes_[runtime_index].child_count;
    return true;
  }

  const std::uint32_t delay = instructions_[slot_index].Execute();
  if (delay == 0U) {
    return false;
  }
  if (executing_count_ >= kMaxExecuting) {
    DebugLog("Timed instruction capacity exhausted");
    LatchRunStatus(run_status, ControllerStatus::kCapacityExhausted);
    return false;
  }
  executing_[executing_count_++] = {slot_index, runtime_index,
                                    timestamp + delay};
  ++runtimes_[runtime_index].active_count;
  return false;
}

bool prism::Controller::PickDueInstructions(std::uint32_t timestamp,
                                            ControllerStatus& run_status) {
  bool progressed = false;
  for (std::uint32_t depth = group_count_; depth > 0U; --depth) {
    const std::uint32_t target_depth = depth - 1U;
    for (std::uint32_t runtime_index = 0U; runtime_index < group_count_;
         ++runtime_index) {
      GroupRuntime& runtime = runtimes_[runtime_index];
      if (!runtime.active || runtime.depth != target_depth) {
        continue;
      }

      const std::uint32_t elapsed = timestamp - runtime.start_time;
      while (runtime.cursor < runtime.record.instruction_count) {
        const std::uint32_t slot_index =
          group_indices_[runtime.record.index_begin + runtime.cursor];
        if (instructions_[slot_index].GetMark() > elapsed) {
          break;
        }

        if (instructions_[slot_index].GetMark() > 0U) {
          ObservePositiveBoundary(runtime_index);
        }
        ++runtime.cursor;
        progressed = true;
        DebugLogPick(slot_index, elapsed);
        if (DispatchDueInstruction(runtime_index, slot_index, timestamp,
                                   run_status)) {
          return true;
        }
      }
    }
  }
  return progressed;
}

void prism::Controller::ScheduleNextRun(std::uint32_t timestamp) {
  for (std::uint32_t i = 0U; i < executing_count_; ++i) {
    const std::uint32_t deadline = executing_[i].deadline;
    if (!DeadlineReached(timestamp, deadline)) {
      ScheduleTimeout(deadline - timestamp);
    }
  }

  for (std::uint32_t i = 0U; i < group_count_; ++i) {
    const GroupRuntime& runtime = runtimes_[i];
    if (!runtime.active || runtime.cursor >= runtime.record.instruction_count) {
      continue;
    }
    const std::uint32_t elapsed = timestamp - runtime.start_time;
    const std::uint32_t slot_index =
      group_indices_[runtime.record.index_begin + runtime.cursor];
    const Mark next_mark = instructions_[slot_index].GetMark();
    if (next_mark > elapsed) {
      ScheduleTimeout(next_mark - elapsed);
    }
  }

  if (min_scheduled_timeout_ > 0U && schedule_next_run_ != nullptr) {
    DebugLog("schedule_next_run_(%u)", min_scheduled_timeout_);
    schedule_next_run_(min_scheduled_timeout_);
  }
}

void prism::Controller::DebugLogDrain(std::uint32_t i,
                                      std::uint32_t idx) const {
  char desc[64];
  instructions_[idx].ToString(desc, sizeof(desc));
  DebugLog("DrainExecuting[%u] idx=%u (%s)", i, idx, desc);
}

void prism::Controller::DebugLogPick(std::uint32_t idx,
                                     std::uint32_t elapsed) const {
  char desc[64];
  instructions_[idx].ToString(desc, sizeof(desc));
  DebugLog("PickDueInstructions idx=%u mark=%u elapsed=%u (%s)", idx,
           static_cast<unsigned>(instructions_[idx].GetMark()),
           static_cast<unsigned>(elapsed), desc);
}

void prism::Controller::DebugLog(const char* format, ...) const {
  if (debug_ == nullptr) {
    return;
  }
  // Build a prefixed format string so the "[Controller] " tag and the
  // message are written atomically through one Vprintf call.
  char prefixed[kDebugLogBufferSize];
  const int n =
    std::snprintf(prefixed, sizeof(prefixed), "[Controller] %s\n", format);
  if (n < 0 || static_cast<std::size_t>(n) >= sizeof(prefixed)) {
    return;
  }
  std::va_list args;
  va_start(args, format);
  debug_->Vprintf(prefixed, args);
  va_end(args);
}
