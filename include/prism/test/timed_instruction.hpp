#ifndef PRISM_TEST_TIMED_INSTRUCTION_HPP_
#define PRISM_TEST_TIMED_INSTRUCTION_HPP_

#include "prism/controller_instruction.hpp"

namespace prism {

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
  /// @param timestamp Timestamp shared by this controller progress tick; this
  ///     test instruction uses its configured delay instead.
  /// @return The configured delay on the first call, then zero.
  std::uint32_t Execute(std::uint32_t timestamp) override;

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

}  // namespace prism

#endif  // PRISM_TEST_TIMED_INSTRUCTION_HPP_
