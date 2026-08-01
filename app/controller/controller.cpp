#include "prism/controller.hpp"

#include <cstdarg>
#include <cstdint>
#include <cstdio>

#include "prism/debug.hpp"
#include "prism/strip.hpp"

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

void prism::Controller::AddInstruction(const ControllerInstruction* instr) {
  if (instruction_count_ >= kMaxInstruction) {
    DebugLog("AddInstruction queue full (%u)", kMaxInstruction);
    return;
  }
  DebugLog("AddInstruction tag=%hhu mark=%u",
           static_cast<std::uint8_t>(instr->Tag()),
           static_cast<unsigned>(instr->mark));

  // Store the instruction at the next free slot.
  const std::uint32_t slot_idx = instruction_count_;
  auto& slot = instructions_[slot_idx];
  slot.Set(instr);
  slot.SetStrip(strip_);
  slot.SetController(this);
  ++instruction_count_;

  // Insert into the mark-sorted pending queue.
  pending_.Insert(slot_idx);
}

void prism::Controller::ResetInstructions() {
  DebugLog("ResetInstructions");
  instruction_count_ = 0U;
  executing_count_ = 0U;
  has_run_ = false;
  pending_.Reset();
}

void prism::Controller::Run() {
  DebugLog("Run");
  show_requested_ = false;
  min_scheduled_timeout_ = 0U;

  // First Run() snapshots the start time; marks are relative offsets.
  if (!has_run_) {
    start_time_ = GetTimestamp();
    has_run_ = true;
  }

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
