#if defined(PRISM_CONTROLLER_TESTING)
#include <cstdio>

#include "prism/test/timed_instruction.hpp"

prism::test::TimedInstruction::TimedInstruction(std::uint32_t delay_ms,
                                                std::uint32_t* execution_count,
                                                bool yield_on_first_execution)
    : delay_ms_{delay_ms},
      execution_count_{execution_count},
      yield_on_first_execution_{yield_on_first_execution} {
  tag_ = InstructionTag::kTestTimed;
}

std::uint32_t prism::test::TimedInstruction::Execute(std::uint32_t) {
  if (execution_count_ != nullptr) {
    ++*execution_count_;
  }
  if (!yield_on_first_execution_ || yielded_) {
    return 0U;
  }
  yielded_ = true;
  return delay_ms_;
}

void prism::test::TimedInstruction::ToString(char* buf,
                                             std::size_t size) const {
  std::snprintf(buf, size, "TimedInstruction(delay=%u)",
                static_cast<unsigned>(delay_ms_));
}

void prism::test::TimedInstruction::ResetForReplay() { yielded_ = false; }

bool prism::test::TimedInstruction::HasPositiveSchedulingBoundary() const {
  return delay_ms_ > 0U;
}
#endif
