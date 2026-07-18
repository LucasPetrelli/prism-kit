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
      head_index_(0U),
      executing_count_(0U),
      block_count_(0U),
      get_timestamp_(nullptr),
      schedule_next_run_(nullptr),
      debug_(nullptr) {}

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

void prism::Controller::Block() {
  DebugLog("Block cnt=%u", block_count_ + 1U);
  ++block_count_;
}

void prism::Controller::Unblock() {
  DebugLog("Unblock cnt=%u", (block_count_ > 0U) ? block_count_ - 1U : 0U);
  if (block_count_ > 0U) {
    --block_count_;
  }
}

bool prism::Controller::IsBlocked() const {
  DebugLog("IsBlocked => %u", block_count_ > 0U);
  return block_count_ > 0U;
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
  DebugLog("AddInstruction tag=%hhu", static_cast<std::uint8_t>(instr->Tag()));
  auto& slot = instructions_[instruction_count_];
  slot.Set(instr);
  slot.SetStrip(strip_);
  slot.SetController(this);
  ++instruction_count_;
}

void prism::Controller::ResetInstructions() {
  DebugLog("ResetInstructions");
  instruction_count_ = 0U;
  head_index_ = 0U;
  executing_count_ = 0U;
  block_count_ = 0U;
}

void prism::Controller::Run() {
  DebugLog("Run");
  show_requested_ = false;
  min_scheduled_timeout_ = 0U;

  DrainExecuting();
  PickNewInstructions();

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
    char desc[64];
    instructions_[idx].ToString(desc, sizeof(desc));
    DebugLog("DrainExecuting[%u] idx=%u (%s)", i, idx, desc);
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
  while (!IsBlocked() && head_index_ < instruction_count_) {
    char desc[64];
    instructions_[head_index_].ToString(desc, sizeof(desc));
    DebugLog("PickNewInstructions head=%u (%s)", head_index_, desc);
    const std::uint32_t result = instructions_[head_index_].Execute();
    if (result == 0U) {
      // Instant instruction completed.
      ++head_index_;
    } else {
      // Timed instruction: save index in executing array.
      if (executing_count_ < kMaxExecuting) {
        executing_[executing_count_++] = head_index_;
      }
      ++head_index_;
      ScheduleTimeout(result);
      // Loop continues as long as !IsBlocked().  A blocking timed
      // instruction (e.g. Delay) will stop the loop naturally.
    }
  }
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
