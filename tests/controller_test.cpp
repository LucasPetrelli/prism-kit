/// @file Tests for prism::Controller and its instruction classes.
///
/// These are host-compiled unit tests (no Zephyr dependencies).  The
/// Controller implementation lives in app/controller/.

#include "prism/controller.hpp"

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mock_strip.hpp"

// ====================================================================
// Test fixture
// ====================================================================

namespace {

/// @brief Records the last duration passed to the schedule callback.
std::uint32_t g_last_scheduled_delay = 0U;

/// @brief Non-capturing callback for Controller::SetScheduleCallback.
void OnScheduleNextRun(std::uint32_t delay_ms) {
  g_last_scheduled_delay = delay_ms;
}
/// @brief Mutable timestamp for the FakeTimestamp callback.
std::uint32_t g_fake_time = 0U;

/// @brief Returns g_fake_time so tests can control the clock.
std::uint32_t FakeTimestamp() { return g_fake_time; }
class ControllerTest : public ::testing::Test {
 protected:
  void SetUp() override {
    controller_.SetStrip(&mock_strip_);
    controller_.SetTimestampCallback([] { return 0U; });
    controller_.SetScheduleCallback(OnScheduleNextRun);
    g_last_scheduled_delay = 0U;
  }

  prism::test::MockStrip mock_strip_;
  prism::Controller controller_;
};
}  // namespace

// ====================================================================
// SetSingleColor tests
// ====================================================================

/// @brief A SetSingleColor instruction writes the unpacked RgbColor to the
///     correct pixel and commits the frame.
TEST_F(ControllerTest, SetSingleColorDispatchesToCorrectLed) {
  constexpr std::uint8_t target_index = 3U;
  constexpr prism::Color color = prism::Color::kPureGreen;
  const prism::RgbColor expected_rgb = prism::ToRgb(color);

  prism::SetSingleColor instr;
  instr.color = prism::ToRgb(color);
  instr.strip = &mock_strip_;
  instr.index = target_index;

  // Activate a slot and execute.
  instr.controller = &controller_;
  prism::InstructionMemorySlot slot;
  slot.Set(&instr);

  EXPECT_CALL(*mock_strip_.mutable_led(static_cast<std::size_t>(target_index)),
              SetColor(expected_rgb))
    .WillOnce(testing::Return(0));
  EXPECT_CALL(mock_strip_, Show()).Times(0);

  slot.Execute();
}

/// @brief SetSingleColor does nothing when strip is null.
TEST_F(ControllerTest, SetSingleColorWithNullStripIsNoop) {
  constexpr prism::Color color = prism::Color::kPureRed;

  prism::SetSingleColor instr;
  instr.color = prism::ToRgb(color);
  instr.strip = nullptr;
  instr.index = 0U;

  prism::InstructionMemorySlot slot;
  slot.Set(&instr);

  // No interaction with any mock — strip is null.
  slot.Execute();
}

// ====================================================================
// SetMultipleColor tests
// ====================================================================

/// @brief A SetMultipleColor instruction writes the unpacked RgbColor to
///     every pixel in [start, end) and commits the frame.
TEST_F(ControllerTest, SetMultipleColorFillsRange) {
  constexpr prism::Color color = prism::Color::kIceBlue;
  const prism::RgbColor expected_rgb = prism::ToRgb(color);

  prism::SetMultipleColor instr;
  instr.color = prism::ToRgb(color);
  instr.strip = &mock_strip_;
  instr.controller = &controller_;
  instr.range.start = 1U;
  instr.range.end = 3U;

  prism::InstructionMemorySlot slot;
  slot.Set(&instr);

  // Pixels 1 and 2 get set_color; pixel 0 does not.
  EXPECT_CALL(*mock_strip_.mutable_led(1U), SetColor(expected_rgb))
    .WillOnce(testing::Return(0));
  EXPECT_CALL(*mock_strip_.mutable_led(2U), SetColor(expected_rgb))
    .WillOnce(testing::Return(0));
  EXPECT_CALL(mock_strip_, Show()).Times(0);

  slot.Execute();
}

// ====================================================================
// Controller::Run() tests
// ====================================================================

/// @brief Run() iterates through all populated instruction slots.
TEST_F(ControllerTest, RunIteratesAllSlots) {
  prism::SetSingleColor r;
  r.color = prism::ToRgb(prism::Color::kPureRed);
  r.strip = &mock_strip_;
  r.index = 0U;
  controller_.AddInstruction(&r);

  prism::SetSingleColor g;
  g.color = prism::ToRgb(prism::Color::kPureGreen);
  g.strip = &mock_strip_;
  g.index = 1U;
  controller_.AddInstruction(&g);

  prism::SetSingleColor b;
  b.color = prism::ToRgb(prism::Color::kPureBlue);
  b.strip = &mock_strip_;
  b.index = 2U;
  controller_.AddInstruction(&b);

  // Controller calls show() once after all instructions.
  EXPECT_CALL(mock_strip_, Show()).Times(1);

  controller_.Run();
}

/// @brief Run() with zero instructions is a no-op.
TEST_F(ControllerTest, RunOnEmptyControllerIsNoop) {
  EXPECT_CALL(mock_strip_, Show()).Times(0);
  controller_.Run();
}

/// @brief ResetInstructions() clears the queue so Run() does nothing.
TEST_F(ControllerTest, ResetInstructionsClearsState) {
  prism::SetSingleColor gold;
  gold.color = prism::ToRgb(prism::Color::kChristmasGold);
  gold.strip = &mock_strip_;
  gold.index = 4U;
  controller_.AddInstruction(&gold);

  prism::SetSingleColor pink;
  pink.color = prism::ToRgb(prism::Color::kCyberpunkPink);
  pink.strip = &mock_strip_;
  pink.index = 5U;
  controller_.AddInstruction(&pink);

  controller_.ResetInstructions();

  EXPECT_CALL(mock_strip_, Show()).Times(0);
  controller_.Run();
}

// ====================================================================
// InstructionMemorySlot tests
// ====================================================================

/// @brief execute() dispatches through the correct vtable regardless of
///     which member was constructed.
TEST_F(ControllerTest, SlotExecuteDispatchesCorrectly) {
  constexpr prism::Color color = prism::Color::kPureWhite;

  // Set up a SetSingleColor instruction.
  prism::SetSingleColor single;
  single.color = prism::ToRgb(color);
  single.strip = &mock_strip_;
  single.controller = &controller_;
  single.index = 5U;

  prism::InstructionMemorySlot slot;
  slot.Set(&single);

  EXPECT_CALL(*mock_strip_.mutable_led(5U), SetColor(testing::_))
    .WillOnce(testing::Return(0));
  EXPECT_CALL(mock_strip_, Show()).Times(0);

  slot.Execute();
}

/// @brief A slot can be reused by calling set() again with a different type.
TEST_F(ControllerTest, SlotCanBeReused) {
  constexpr prism::Color color = prism::Color::kPureRed;

  // First: SetSingleColor.
  prism::SetSingleColor single;
  single.color = prism::ToRgb(color);
  single.strip = &mock_strip_;
  single.controller = &controller_;
  single.index = 2U;

  prism::InstructionMemorySlot slot;
  slot.Set(&single);

  EXPECT_CALL(*mock_strip_.mutable_led(2U), SetColor(testing::_))
    .WillOnce(testing::Return(0));
  EXPECT_CALL(mock_strip_, Show()).Times(0);
  slot.Execute();

  // Reset the mock expectations for the second execution.
  testing::Mock::VerifyAndClearExpectations(&mock_strip_);

  // Reuse the slot as SetMultipleColor.
  prism::SetMultipleColor multi;
  multi.color = prism::ToRgb(prism::Color::kPureBlue);
  multi.strip = &mock_strip_;
  multi.controller = &controller_;
  multi.range.start = 0U;
  multi.range.end = 2U;

  slot.Set(&multi);

  EXPECT_CALL(*mock_strip_.mutable_led(0U), SetColor(testing::_))
    .WillOnce(testing::Return(0));
  EXPECT_CALL(*mock_strip_.mutable_led(1U), SetColor(testing::_))
    .WillOnce(testing::Return(0));
  EXPECT_CALL(mock_strip_, Show()).Times(0);
  slot.Execute();
}

// ====================================================================
// Delay instruction tests
// ====================================================================

/// @brief A single Delay blocks the controller for its duration, then
///     completes on the second Run() call.
TEST_F(ControllerTest, SingleDelayBlocksAndCompletes) {
  g_fake_time = 1U;
  controller_.SetTimestampCallback(FakeTimestamp);

  prism::Delay delay(100U);
  controller_.AddInstruction(&delay);

  EXPECT_CALL(mock_strip_, Show()).Times(0);

  // Run 1 at t=1: Delay blocks and returns 100.
  controller_.Run();
  EXPECT_TRUE(controller_.IsBlocked());
  EXPECT_EQ(g_last_scheduled_delay, 100U);

  // Run 2 at t=101: elapsed >= 100, Delay completes.
  g_fake_time = 101U;
  controller_.Run();
  EXPECT_FALSE(controller_.IsBlocked());
}

/// @brief A Delay between two SetSingleColor instructions pauses execution:
///     the first Set runs, then Delay blocks.  A second Run() completes the
///     Delay and runs the second Set.
TEST_F(ControllerTest, DelayBetweenTwoSets) {
  g_fake_time = 1U;
  controller_.SetTimestampCallback(FakeTimestamp);

  constexpr prism::RgbColor red = {255U, 0U, 0U};
  constexpr prism::RgbColor blue = {0U, 0U, 255U};

  prism::SetSingleColor first;
  first.color = red;
  first.strip = &mock_strip_;
  first.index = 0U;
  controller_.AddInstruction(&first);

  prism::Delay delay(50U);
  controller_.AddInstruction(&delay);

  prism::SetSingleColor second;
  second.color = blue;
  second.strip = &mock_strip_;
  second.index = 1U;
  controller_.AddInstruction(&second);

  // Run 1 at t=1: first Set executes; Delay blocks and returns 50.
  EXPECT_CALL(*mock_strip_.mutable_led(0U), SetColor(red)).Times(1);
  EXPECT_CALL(mock_strip_, Show()).Times(1);

  controller_.Run();
  EXPECT_TRUE(controller_.IsBlocked());
  EXPECT_EQ(g_last_scheduled_delay, 50U);

  // Run 2 at t=51: elapsed >= 50, Delay completes; second Set executes.
  g_fake_time = 51U;
  EXPECT_CALL(*mock_strip_.mutable_led(1U), SetColor(blue)).Times(1);
  EXPECT_CALL(mock_strip_, Show()).Times(1);

  controller_.Run();
  EXPECT_FALSE(controller_.IsBlocked());
}

/// @brief Two delays interleaved with three SetSingleColor instructions:
///     each Run() advances one step through the sequence.
TEST_F(ControllerTest, TwoDelaysBetweenSets) {
  g_fake_time = 1U;
  controller_.SetTimestampCallback(FakeTimestamp);

  constexpr prism::RgbColor red = {255U, 0U, 0U};
  constexpr prism::RgbColor green = {0U, 255U, 0U};
  constexpr prism::RgbColor blue = {0U, 0U, 255U};

  prism::SetSingleColor a;
  a.color = red;
  a.strip = &mock_strip_;
  a.index = 0U;
  controller_.AddInstruction(&a);

  prism::Delay delay1(30U);
  controller_.AddInstruction(&delay1);

  prism::SetSingleColor b;
  b.color = green;
  b.strip = &mock_strip_;
  b.index = 1U;
  controller_.AddInstruction(&b);

  prism::Delay delay2(70U);
  controller_.AddInstruction(&delay2);

  prism::SetSingleColor c;
  c.color = blue;
  c.strip = &mock_strip_;
  c.index = 2U;
  controller_.AddInstruction(&c);

  // Run 1 at t=1: first Set runs; delay1 blocks (30 ms).
  EXPECT_CALL(*mock_strip_.mutable_led(0U), SetColor(red)).Times(1);
  EXPECT_CALL(mock_strip_, Show()).Times(1);

  controller_.Run();
  EXPECT_TRUE(controller_.IsBlocked());
  EXPECT_EQ(g_last_scheduled_delay, 30U);

  // Run 2 at t=31: delay1 elapsed; second Set runs; delay2 blocks (70 ms).
  g_fake_time = 31U;
  EXPECT_CALL(*mock_strip_.mutable_led(1U), SetColor(green)).Times(1);
  EXPECT_CALL(mock_strip_, Show()).Times(1);

  controller_.Run();
  EXPECT_TRUE(controller_.IsBlocked());
  EXPECT_EQ(g_last_scheduled_delay, 70U);

  // Run 3 at t=101: delay2 elapsed; third Set runs; all done.
  g_fake_time = 101U;
  EXPECT_CALL(*mock_strip_.mutable_led(2U), SetColor(blue)).Times(1);
  EXPECT_CALL(mock_strip_, Show()).Times(1);

  controller_.Run();
  EXPECT_FALSE(controller_.IsBlocked());
}

/// @brief A Delay whose start and subsequent Execute() calls happen at
///     different timestamps returns only the remaining time.  If elapsed
///     exceeds the delay, it completes.
TEST_F(ControllerTest, DelayRespectsElapsedTime) {
  g_fake_time = 1000U;
  controller_.SetTimestampCallback(FakeTimestamp);

  prism::Delay delay(2000U);
  controller_.AddInstruction(&delay);

  // Run 1 at t=1000: first call — block and return full 2000ms.
  controller_.Run();
  EXPECT_TRUE(controller_.IsBlocked());
  EXPECT_EQ(g_last_scheduled_delay, 2000U);

  // Run 2 at t=2000: elapsed=1000ms, remaining=1000ms.
  g_fake_time = 2000U;
  controller_.Run();
  EXPECT_TRUE(controller_.IsBlocked());
  EXPECT_EQ(g_last_scheduled_delay, 1000U);

  // Run 3 at t=3000: elapsed=2000ms, done.
  g_fake_time = 3000U;
  controller_.Run();
  EXPECT_FALSE(controller_.IsBlocked());
}
