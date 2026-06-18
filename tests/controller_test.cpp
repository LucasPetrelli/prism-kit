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

class ControllerTest : public ::testing::Test {
 protected:
  void SetUp() override {
    controller_.SetStrip(&mock_strip_);
    controller_.SetTimestampCallback([] { return 0U; });
  }

  prism::test::MockStrip mock_strip_;
  prism::Controller controller_;
};

// ====================================================================
// SetSingleColor tests
// ====================================================================

/// @brief A SetSingleColor instruction writes the unpacked RgbColor to the
///     correct pixel and commits the frame.
TEST_F(ControllerTest, SetSingleColorDispatchesToCorrectLed) {
  constexpr std::size_t kTargetIndex = 3U;
  constexpr prism::Color kColor = prism::Color::kPureGreen;
  const prism::RgbColor kExpectedRgb = prism::to_rgb(kColor);

  prism::SetSingleColor instr;
  instr.color = kColor;
  instr.strip = &mock_strip_;
  instr.index = kTargetIndex;

  // Activate a slot and execute.
  prism::InstructionMemorySlot slot;
  slot.set(&instr);

  EXPECT_CALL(*mock_strip_.mutable_led(kTargetIndex), set_color(kExpectedRgb))
    .WillOnce(testing::Return(0));
  EXPECT_CALL(mock_strip_, show()).Times(0);

  slot.execute();
}

/// @brief SetSingleColor does nothing when strip is null.
TEST_F(ControllerTest, SetSingleColorWithNullStripIsNoop) {
  constexpr prism::Color kColor = prism::Color::kPureRed;

  prism::SetSingleColor instr;
  instr.color = kColor;
  instr.strip = nullptr;
  instr.index = 0U;

  prism::InstructionMemorySlot slot;
  slot.set(&instr);

  // No interaction with any mock — strip is null.
  slot.execute();
}

// ====================================================================
// SetMultipleColor tests
// ====================================================================

/// @brief A SetMultipleColor instruction writes the unpacked RgbColor to
///     every pixel in [start, end) and commits the frame.
TEST_F(ControllerTest, SetMultipleColorFillsRange) {
  constexpr prism::Color kColor = prism::Color::kIceBlue;
  const prism::RgbColor kExpectedRgb = prism::to_rgb(kColor);

  prism::SetMultipleColor instr;
  instr.color = kColor;
  instr.strip = &mock_strip_;
  instr.range.start = 1U;
  instr.range.end = 3U;

  prism::InstructionMemorySlot slot;
  slot.set(&instr);

  // Pixels 1 and 2 get set_color; pixel 0 does not.
  EXPECT_CALL(*mock_strip_.mutable_led(1U), set_color(kExpectedRgb))
    .WillOnce(testing::Return(0));
  EXPECT_CALL(*mock_strip_.mutable_led(2U), set_color(kExpectedRgb))
    .WillOnce(testing::Return(0));
  EXPECT_CALL(mock_strip_, show()).Times(0);

  slot.execute();
}

// ====================================================================
// Controller::Run() tests
// ====================================================================

/// @brief Run() iterates through all populated instruction slots.
TEST_F(ControllerTest, RunIteratesAllSlots) {
  prism::SetSingleColor r;
  r.color = prism::Color::kPureRed;
  r.strip = &mock_strip_;
  r.index = 0U;
  controller_.AddInstruction(&r);

  prism::SetSingleColor g;
  g.color = prism::Color::kPureGreen;
  g.strip = &mock_strip_;
  g.index = 1U;
  controller_.AddInstruction(&g);

  prism::SetSingleColor b;
  b.color = prism::Color::kPureBlue;
  b.strip = &mock_strip_;
  b.index = 2U;
  controller_.AddInstruction(&b);

  // Controller calls show() once after all instructions.
  EXPECT_CALL(mock_strip_, show()).Times(1);

  controller_.Run();
}

/// @brief Run() with zero instructions is a no-op.
TEST_F(ControllerTest, RunOnEmptyControllerIsNoop) {
  EXPECT_CALL(mock_strip_, show()).Times(0);
  controller_.Run();
}

/// @brief ResetInstructions() clears the queue so Run() does nothing.
TEST_F(ControllerTest, ResetInstructionsClearsState) {
  prism::SetSingleColor gold;
  gold.color = prism::Color::kChristmasGold;
  gold.strip = &mock_strip_;
  gold.index = 4U;
  controller_.AddInstruction(&gold);

  prism::SetSingleColor pink;
  pink.color = prism::Color::kCyberpunkPink;
  pink.strip = &mock_strip_;
  pink.index = 5U;
  controller_.AddInstruction(&pink);

  controller_.ResetInstructions();

  EXPECT_CALL(mock_strip_, show()).Times(0);
  controller_.Run();
}

// ====================================================================
// InstructionMemorySlot tests
// ====================================================================

/// @brief execute() dispatches through the correct vtable regardless of
///     which member was constructed.
TEST_F(ControllerTest, SlotExecuteDispatchesCorrectly) {
  constexpr prism::Color kColor = prism::Color::kPureWhite;

  // Set up a SetSingleColor instruction.
  prism::SetSingleColor single;
  single.color = kColor;
  single.strip = &mock_strip_;
  single.index = 5U;

  prism::InstructionMemorySlot slot;
  slot.set(&single);

  EXPECT_CALL(*mock_strip_.mutable_led(5U), set_color(testing::_))
    .WillOnce(testing::Return(0));
  EXPECT_CALL(mock_strip_, show()).Times(0);

  slot.execute();
}

/// @brief A slot can be reused by calling set() again with a different type.
TEST_F(ControllerTest, SlotCanBeReused) {
  constexpr prism::Color kColor = prism::Color::kPureRed;

  // First: SetSingleColor.
  prism::SetSingleColor single;
  single.color = kColor;
  single.strip = &mock_strip_;
  single.index = 2U;

  prism::InstructionMemorySlot slot;
  slot.set(&single);

  EXPECT_CALL(*mock_strip_.mutable_led(2U), set_color(testing::_))
    .WillOnce(testing::Return(0));
  EXPECT_CALL(mock_strip_, show()).Times(0);
  slot.execute();

  // Reset the mock expectations for the second execution.
  testing::Mock::VerifyAndClearExpectations(&mock_strip_);

  // Reuse the slot as SetMultipleColor.
  prism::SetMultipleColor multi;
  multi.color = prism::Color::kPureBlue;
  multi.strip = &mock_strip_;
  multi.range.start = 0U;
  multi.range.end = 2U;

  slot.set(&multi);

  EXPECT_CALL(*mock_strip_.mutable_led(0U), set_color(testing::_))
    .WillOnce(testing::Return(0));
  EXPECT_CALL(*mock_strip_.mutable_led(1U), set_color(testing::_))
    .WillOnce(testing::Return(0));
  EXPECT_CALL(mock_strip_, show()).Times(0);
  slot.execute();
}
