/// @file Tests for prism::Controller and its instruction classes.
///
/// These are host-compiled unit tests (no Zephyr dependencies).  The
/// Controller and InstructionMemorySlot member functions are defined here as
/// minimal stubs until the production implementations exist.

#include "prism/controller.hpp"

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mock_strip.hpp"
#include "prism/color.hpp"

// ====================================================================
// Stub instruction and controller implementations for testing.
//
// Once the production .cpp file is written, remove these stubs and link
// the test binary against the real implementation instead.
// ====================================================================

void prism::InstructionMemorySlot::set(const SetMultipleColor* instr) {
  ::new (&setMultipleColor) SetMultipleColor;
  setMultipleColor.color = instr->color;
  setMultipleColor.strip = instr->strip;
  setMultipleColor.range[0] = instr->range[0];
  setMultipleColor.range[1] = instr->range[1];
}

void prism::InstructionMemorySlot::set(const SetSingleColor* instr) {
  ::new (&setSingleColor) SetSingleColor;
  setSingleColor.color = instr->color;
  setSingleColor.strip = instr->strip;
  setSingleColor.index = instr->index;
}

void prism::InstructionMemorySlot::execute() {
  static_cast<ControllerInstruction*>(&setSingleColor)->Execute();
}

prism::Controller::Controller()
    : strip_(nullptr), instruction_count_(0U), get_timestamp_(nullptr) {}

void prism::Controller::SetStrip(Strip* strip) { strip_ = strip; }

void prism::Controller::SetTimestampCallback(TimestampCallback cb) {
  get_timestamp_ = cb;
}

void prism::Controller::AddInstruction(Color color, std::uint32_t index) {
  if (instruction_count_ >= kMaxInstruction) {
    return;
  }
  SetSingleColor instr;
  instr.color = color;
  instr.strip = strip_;
  instr.index = index;
  instructions_[instruction_count_].set(&instr);
  ++instruction_count_;
}

void prism::Controller::ResetInstructions() { instruction_count_ = 0U; }

void prism::Controller::Run() {
  for (std::uint32_t i = 0U; i < instruction_count_; ++i) {
    instructions_[i].execute();
  }
}

// ====================================================================
// Instruction Execute() method implementations
// ====================================================================

namespace {

/// @brief Unpack Color into RgbColor and stage it on every pixel in the
///     active range, then commit the frame.
void set_multiple_color_impl(prism::SetMultipleColor* self) {
  const prism::RgbColor rgb = prism::to_rgb(self->color);
  if (self->strip == nullptr) {
    return;
  }
  for (std::uint32_t i = self->range[0]; i < self->range[1]; ++i) {
    prism::StripLed* led = self->strip->led(i);
    if (led != nullptr) {
      led->set_color(rgb);
    }
  }
  self->strip->show();
}

/// @brief Unpack Color into RgbColor, stage it on a single pixel, then
///     commit the frame.
void set_single_color_impl(prism::SetSingleColor* self) {
  const prism::RgbColor rgb = prism::to_rgb(self->color);
  if (self->strip == nullptr) {
    return;
  }
  prism::StripLed* led = self->strip->led(self->index);
  if (led != nullptr) {
    led->set_color(rgb);
  }
  self->strip->show();
}

}  // namespace

void prism::SetMultipleColor::Execute() { set_multiple_color_impl(this); }
void prism::SetSingleColor::Execute() { set_single_color_impl(this); }

// ====================================================================
// Test fixture
// ====================================================================

class ControllerTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // Default strip is ready, with 8 leds wired to mutable_led().
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
  EXPECT_CALL(mock_strip_, show()).Times(1);

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
  instr.range[0] = 1U;
  instr.range[1] = 3U;

  prism::InstructionMemorySlot slot;
  slot.set(&instr);

  // Pixels 1 and 2 get set_color; pixel 0 does not.
  EXPECT_CALL(*mock_strip_.mutable_led(1U), set_color(kExpectedRgb))
    .WillOnce(testing::Return(0));
  EXPECT_CALL(*mock_strip_.mutable_led(2U), set_color(kExpectedRgb))
    .WillOnce(testing::Return(0));
  EXPECT_CALL(mock_strip_, show()).Times(1);

  slot.execute();
}

// ====================================================================
// Controller::Run() tests
// ====================================================================

/// @brief Run() iterates through all populated instruction slots.
TEST_F(ControllerTest, RunIteratesAllSlots) {
  controller_.AddInstruction(prism::Color::kPureRed, 0U);
  controller_.AddInstruction(prism::Color::kPureGreen, 1U);
  controller_.AddInstruction(prism::Color::kPureBlue, 2U);

  // Each instruction calls show() once.
  EXPECT_CALL(mock_strip_, show()).Times(3);

  controller_.Run();
}

/// @brief Run() with zero instructions is a no-op.
TEST_F(ControllerTest, RunOnEmptyControllerIsNoop) {
  EXPECT_CALL(mock_strip_, show()).Times(0);
  controller_.Run();
}

/// @brief ResetInstructions() clears the queue so Run() does nothing.
TEST_F(ControllerTest, ResetInstructionsClearsState) {
  controller_.AddInstruction(prism::Color::kChristmasGold, 4U);
  controller_.AddInstruction(prism::Color::kCyberpunkPink, 5U);
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
  EXPECT_CALL(mock_strip_, show()).Times(1);

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
  EXPECT_CALL(mock_strip_, show()).Times(1);
  slot.execute();

  // Reset the mock expectations for the second execution.
  testing::Mock::VerifyAndClearExpectations(&mock_strip_);

  // Reuse the slot as SetMultipleColor.
  prism::SetMultipleColor multi;
  multi.color = prism::Color::kPureBlue;
  multi.strip = &mock_strip_;
  multi.range[0] = 0U;
  multi.range[1] = 2U;

  slot.set(&multi);

  EXPECT_CALL(*mock_strip_.mutable_led(0U), set_color(testing::_))
    .WillOnce(testing::Return(0));
  EXPECT_CALL(*mock_strip_.mutable_led(1U), set_color(testing::_))
    .WillOnce(testing::Return(0));
  EXPECT_CALL(mock_strip_, show()).Times(1);
  slot.execute();
}
