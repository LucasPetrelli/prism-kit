#include "prism/controller.hpp"

// ====================================================================
// Controller
// ====================================================================

prism::Controller::Controller()
    : strip_(nullptr), instruction_count_(0U), get_timestamp_(nullptr) {}

void prism::Controller::SetStrip(Strip* strip) { strip_ = strip; }

void prism::Controller::SetTimestampCallback(TimestampCallback callback) {
  get_timestamp_ = callback;
}

void prism::Controller::AddInstruction(ControllerInstruction* instr) {
  if (instruction_count_ >= kMaxInstruction) {
    return;
  }
  instructions_[instruction_count_++].set(instr);
}

void prism::Controller::ResetInstructions() { instruction_count_ = 0U; }

void prism::Controller::Run() {
  if (instruction_count_ == 0U) {
    return;
  }
  for (std::uint32_t i = 0U; i < instruction_count_; ++i) {
    instructions_[i].execute();
  }
  if (strip_ != nullptr) {
    strip_->show();
  }
}
