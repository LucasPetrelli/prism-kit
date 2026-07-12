#include "prism/controller.hpp"

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
      schedule_next_run_(nullptr) {}

void prism::Controller::SetStrip(Strip* strip) { strip_ = strip; }

void prism::Controller::SetTimestampCallback(TimestampCallback callback) {
  get_timestamp_ = callback;
}

void prism::Controller::SetScheduleCallback(ScheduleCallback callback) {
  schedule_next_run_ = callback;
}

void prism::Controller::Block() { ++block_count_; }

void prism::Controller::Unblock() {
  if (block_count_ > 0U) {
    --block_count_;
  }
}

bool prism::Controller::IsBlocked() const { return block_count_ > 0U; }

void prism::Controller::RequestShow() { show_requested_ = true; }

std::uint32_t prism::Controller::GetTimestamp() const {
  return (get_timestamp_ != nullptr) ? get_timestamp_() : 0U;
}

void prism::Controller::ScheduleTimeout(std::uint32_t ms) {
  if (min_scheduled_timeout_ == 0U || ms < min_scheduled_timeout_) {
    min_scheduled_timeout_ = ms;
  }
}

void prism::Controller::AddInstruction(const ControllerInstruction* instr) {
  if (instruction_count_ >= kMaxInstruction) {
    return;
  }
  auto& slot = instructions_[instruction_count_];
  slot.Set(instr);
  slot.SetStrip(strip_);
  slot.SetController(this);
  ++instruction_count_;
}

void prism::Controller::ResetInstructions() {
  instruction_count_ = 0U;
  head_index_ = 0U;
  executing_count_ = 0U;
  block_count_ = 0U;
}

void prism::Controller::Run() {
  show_requested_ = false;
  min_scheduled_timeout_ = 0U;

  DrainExecuting();
  PickNewInstructions();

  if (min_scheduled_timeout_ > 0U && schedule_next_run_ != nullptr) {
    schedule_next_run_(min_scheduled_timeout_);
  }

  if (show_requested_ && strip_ != nullptr) {
    strip_->Show();
  }
}

void prism::Controller::DrainExecuting() {
  std::uint32_t i = 0U;
  while (i < executing_count_) {
    const std::uint32_t idx = executing_[i];
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
