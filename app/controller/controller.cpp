#include "prism/controller.hpp"

#include <cstdarg>
#include <cstdint>
#include <cstdio>

#include "prism/debug.hpp"
#include "prism/instruction.hpp"
#include "prism/strip.hpp"

static_assert(prism::kMaxGroups <= 32U,
              "Group graph masks require at most 32 groups");

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
      debug_(nullptr) {
  pending_.SetInstructions(instructions_);
}

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

  // Preserve insertion order inside each group's shared index range.
  for (std::uint32_t i = 0U; i < instruction_count_; ++i) {
    const std::uint32_t group_index =
      FindGroup(build.groups, build.group_count, instructions_[i].GetGroupId());
    build.instruction_indices[build.index_write_positions[group_index]++] = i;
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

void prism::Controller::ResetInstructionReplayState() {
  for (std::uint32_t i = 0U; i < instruction_count_; ++i) {
    instructions_[i].ResetForReplay();
  }
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

  ResetInstructionReplayState();
  executing_count_ = 0U;
  pending_.Reset();
  const std::uint32_t root_index =
    FindGroup(groups_, group_count_, GroupId{0U});
  if (root_index != kMaxGroups) {
    const GroupRecord& root = groups_[root_index];
    for (std::uint32_t i = 0U; i < root.instruction_count; ++i) {
      pending_.Insert(group_indices_[root.index_begin + i]);
    }
  }

  root_additional_repeats_ = root_additional_repeats;
  start_time_ = GetTimestamp();
  has_run_ = true;
  show_requested_ = false;
  min_scheduled_timeout_ = 0U;
  state_ =
    (root_index == kMaxGroups || groups_[root_index].instruction_count == 0U)
      ? ProgramState::kCompleted
      : ProgramState::kActive;
  return ControllerStatus::kSuccess;
}

void prism::Controller::ResetInstructions() {
  DebugLog("ResetInstructions");
  for (std::uint32_t i = 0U; i < kMaxInstruction; ++i) {
    instructions_[i].Clear();
  }
  instruction_count_ = 0U;
  executing_count_ = 0U;
  has_run_ = false;
  state_ = ProgramState::kEditable;
  root_additional_repeats_ = 0U;
  group_count_ = 0U;
  for (std::uint32_t i = 0U; i < kMaxGroups; ++i) {
    groups_[i] = GroupRecord{};
    runtimes_[i] = GroupRuntime{};
  }
  for (std::uint32_t i = 0U; i < kMaxInstructions; ++i) {
    group_indices_[i] = 0U;
  }
  pending_.Reset();
}

prism::ControllerStatus prism::Controller::Run() {
  if (state_ == ProgramState::kEditable) {
    if (instruction_count_ == 0U) {
      return ControllerStatus::kSuccess;
    }
    // Keep the pre-Start call pattern working for existing local users while
    // the APP start-command migration is completed in a later step.
    const ControllerStatus status = Start(0U);
    if (status != ControllerStatus::kSuccess) {
      return status;
    }
  }
  if (state_ != ProgramState::kActive) {
    return ControllerStatus::kSuccess;
  }

  DebugLog("Run");
  show_requested_ = false;
  min_scheduled_timeout_ = 0U;

  DrainExecuting();
  PickNewInstructions();

  // Look-ahead: if there is a pending instruction whose mark hasn't
  // been reached yet, schedule a wakeup for when it becomes due.
  if (pending_.HasNext()) {
    const std::uint32_t elapsed = GetElapsed();
    const Mark next_mark = pending_.PeekMark();
    if (next_mark > elapsed) {
      ScheduleTimeout(next_mark - elapsed);
    }
  }

  if (min_scheduled_timeout_ > 0U && schedule_next_run_ != nullptr) {
    DebugLog("schedule_next_run_(%u)", min_scheduled_timeout_);
    schedule_next_run_(min_scheduled_timeout_);
  }

  if (show_requested_ && strip_ != nullptr) {
    DebugLog("strip->Show()");
    strip_->Show();
  }

  if (!pending_.HasNext() && executing_count_ == 0U) {
    state_ = ProgramState::kCompleted;
  }
  return ControllerStatus::kSuccess;
}

void prism::Controller::DrainExecuting() {
  std::uint32_t i = 0U;
  while (i < executing_count_) {
    const std::uint32_t idx = executing_[i];
    DebugLogDrain(i, idx);
    const std::uint32_t result = instructions_[idx].Execute();
    if (result == 0U) {
      // Instruction completed — swap with last and shrink.
      --executing_count_;
      executing_[i] = executing_[executing_count_];
    } else {
      // Still pending: register timeout for end-of-run scheduling.
      ScheduleTimeout(result);
      ++i;
    }
  }
}

void prism::Controller::PickNewInstructions() {
  const std::uint32_t elapsed = GetElapsed();
  while (pending_.HasNext() && (pending_.PeekMark() <= elapsed)) {
    const std::uint32_t idx = pending_.Peek();
    DebugLogPick(idx, elapsed);
    const std::uint32_t result = instructions_[idx].Execute();
    pending_.Advance();
    if (result != 0U) {
      // Timed instruction: save index in executing array.
      if (executing_count_ < kMaxExecuting) {
        executing_[executing_count_++] = idx;
      }
      ScheduleTimeout(result);
    }
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
  DebugLog("PickNewInstructions idx=%u mark=%u elapsed=%u (%s)", idx,
           static_cast<unsigned>(instructions_[idx].GetMark()),
           static_cast<unsigned>(elapsed), desc);
}

std::uint32_t prism::Controller::GetElapsed() const {
  return GetTimestamp() - start_time_;
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
