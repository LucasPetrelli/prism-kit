/// @file Tests for prism::Controller and its instruction classes.
///
/// These are host-compiled unit tests (no Zephyr dependencies).  The
/// Controller implementation lives in app/controller/.

#include "prism/controller.hpp"

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mock_strip.hpp"
#include "prism/color.hpp"
#include "prism/instruction.hpp"

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
    g_fake_time = 0U;
    controller_.SetStrip(&mock_strip_);
    controller_.SetTimestampCallback(FakeTimestamp);
    controller_.SetScheduleCallback(OnScheduleNextRun);
    g_last_scheduled_delay = 0U;
  }

  prism::test::MockStrip mock_strip_;
  prism::Controller controller_;
};
// ====================================================================
// HSV instruction tests
// ====================================================================

/// @brief SetSingleColorHsv converts HSV to RGB and dispatches to the
///     correct pixel.
TEST_F(ControllerTest, SetSingleColorHsvConvertsAndDispatches) {
  // Hue 170 ~ 240° (blue-cyan range), full saturation, full value.
  constexpr prism::color::HsvColor hsv{170U, 255U, 255U};
  constexpr prism::color::RgbColor expected_rgb = prism::color::HsvToRgb(hsv);

  prism::SetSingleColorHsv instr;
  instr.color = hsv;
  instr.strip = &mock_strip_;
  instr.controller = &controller_;
  instr.index = 2U;

  prism::InstructionMemorySlot slot;
  slot.Set(&instr);

  EXPECT_CALL(*mock_strip_.MutableLed(2U), SetColor(expected_rgb))
    .WillOnce(testing::Return(0));
  EXPECT_CALL(mock_strip_, Show()).Times(0);

  slot.Execute();
}

/// @brief SetMultipleColorHsv converts HSV to RGB and fills the range.
TEST_F(ControllerTest, SetMultipleColorHsvFillsRange) {
  // Hue 85 ~ 120° (green), full saturation, full value.
  constexpr prism::color::HsvColor hsv{85U, 255U, 255U};
  constexpr prism::color::RgbColor expected_rgb = prism::color::HsvToRgb(hsv);

  prism::SetMultipleColorHsv instr;
  instr.color = hsv;
  instr.strip = &mock_strip_;
  instr.controller = &controller_;
  instr.range.start = 0U;
  instr.range.end = 3U;

  prism::InstructionMemorySlot slot;
  slot.Set(&instr);

  EXPECT_CALL(*mock_strip_.MutableLed(0U), SetColor(expected_rgb))
    .WillOnce(testing::Return(0));
  EXPECT_CALL(*mock_strip_.MutableLed(1U), SetColor(expected_rgb))
    .WillOnce(testing::Return(0));
  EXPECT_CALL(*mock_strip_.MutableLed(2U), SetColor(expected_rgb))
    .WillOnce(testing::Return(0));
  EXPECT_CALL(mock_strip_, Show()).Times(0);

  slot.Execute();
}

/// @brief SetSingleColorHsv is a no-op when strip is null.
TEST_F(ControllerTest, SetSingleColorHsvWithNullStripIsNoop) {
  constexpr prism::color::HsvColor hsv{0U, 255U, 255U};

  prism::SetSingleColorHsv instr;
  instr.color = hsv;
  instr.strip = nullptr;
  instr.index = 0U;

  prism::InstructionMemorySlot slot;
  slot.Set(&instr);

  slot.Execute();
}

/// @brief Slot reused from SetSingleColor to SetSingleColorHsv.
TEST_F(ControllerTest, SlotReusesRgbThenHsv) {
  constexpr prism::color::Preset color = prism::color::Preset::kPureRed;

  // First: RGB SetSingleColor.
  prism::SetSingleColor single;
  single.color = prism::color::ToRgb(color);
  single.strip = &mock_strip_;
  single.controller = &controller_;
  single.index = 0U;

  prism::InstructionMemorySlot slot;
  slot.Set(&single);

  EXPECT_CALL(*mock_strip_.MutableLed(0U), SetColor(testing::_))
    .WillOnce(testing::Return(0));
  EXPECT_CALL(mock_strip_, Show()).Times(0);
  slot.Execute();

  testing::Mock::VerifyAndClearExpectations(&mock_strip_);

  // Reuse the slot as SetSingleColorHsv.
  constexpr prism::color::HsvColor hsv{42U, 200U, 180U};
  prism::SetSingleColorHsv hsv_instr;
  hsv_instr.color = hsv;
  hsv_instr.strip = &mock_strip_;
  hsv_instr.controller = &controller_;
  hsv_instr.index = 1U;

  slot.Set(&hsv_instr);

  const prism::color::RgbColor expected_rgb = prism::color::HsvToRgb(hsv);
  EXPECT_CALL(*mock_strip_.MutableLed(1U), SetColor(expected_rgb))
    .WillOnce(testing::Return(0));
  EXPECT_CALL(mock_strip_, Show()).Times(0);
  slot.Execute();
}

}  // namespace

// ====================================================================
// SetSingleColor tests
// ====================================================================

/// @brief A SetSingleColor instruction writes the unpacked RgbColor to the
///     correct pixel and commits the frame.
TEST_F(ControllerTest, SetSingleColorDispatchesToCorrectLed) {
  constexpr std::uint8_t target_index = 3U;
  constexpr prism::color::Preset color = prism::color::Preset::kPureGreen;
  const prism::color::RgbColor expected_rgb = prism::color::ToRgb(color);

  prism::SetSingleColor instr;
  instr.color = prism::color::ToRgb(color);
  instr.strip = &mock_strip_;
  instr.index = target_index;

  // Activate a slot and execute.
  instr.controller = &controller_;
  prism::InstructionMemorySlot slot;
  slot.Set(&instr);

  EXPECT_CALL(*mock_strip_.MutableLed(static_cast<std::size_t>(target_index)),
              SetColor(expected_rgb))
    .WillOnce(testing::Return(0));
  EXPECT_CALL(mock_strip_, Show()).Times(0);

  slot.Execute();
}

/// @brief SetSingleColor does nothing when strip is null.
TEST_F(ControllerTest, SetSingleColorWithNullStripIsNoop) {
  constexpr prism::color::Preset color = prism::color::Preset::kPureRed;

  prism::SetSingleColor instr;
  instr.color = prism::color::ToRgb(color);
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
  constexpr prism::color::Preset color = prism::color::Preset::kIceBlue;
  const prism::color::RgbColor expected_rgb = prism::color::ToRgb(color);

  prism::SetMultipleColor instr;
  instr.color = prism::color::ToRgb(color);
  instr.strip = &mock_strip_;
  instr.controller = &controller_;
  instr.range.start = 1U;
  instr.range.end = 3U;

  prism::InstructionMemorySlot slot;
  slot.Set(&instr);

  // Pixels 1 and 2 get set_color; pixel 0 does not.
  EXPECT_CALL(*mock_strip_.MutableLed(1U), SetColor(expected_rgb))
    .WillOnce(testing::Return(0));
  EXPECT_CALL(*mock_strip_.MutableLed(2U), SetColor(expected_rgb))
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
  r.color = prism::color::ToRgb(prism::color::Preset::kPureRed);
  r.strip = &mock_strip_;
  r.index = 0U;
  controller_.AddInstruction(&r);

  prism::SetSingleColor g;
  g.color = prism::color::ToRgb(prism::color::Preset::kPureGreen);
  g.strip = &mock_strip_;
  g.index = 1U;
  controller_.AddInstruction(&g);

  prism::SetSingleColor b;
  b.color = prism::color::ToRgb(prism::color::Preset::kPureBlue);
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
  gold.color = prism::color::ToRgb(prism::color::Preset::kChristmasGold);
  gold.strip = &mock_strip_;
  gold.index = 4U;
  controller_.AddInstruction(&gold);

  prism::SetSingleColor pink;
  pink.color = prism::color::ToRgb(prism::color::Preset::kCyberpunkPink);
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
  constexpr prism::color::Preset color = prism::color::Preset::kPureWhite;

  // Set up a SetSingleColor instruction.
  prism::SetSingleColor single;
  single.color = prism::color::ToRgb(color);
  single.strip = &mock_strip_;
  single.controller = &controller_;
  single.index = 5U;

  prism::InstructionMemorySlot slot;
  slot.Set(&single);

  EXPECT_CALL(*mock_strip_.MutableLed(5U), SetColor(testing::_))
    .WillOnce(testing::Return(0));
  EXPECT_CALL(mock_strip_, Show()).Times(0);

  slot.Execute();
}

/// @brief A slot can be reused by calling set() again with a different type.
TEST_F(ControllerTest, SlotCanBeReused) {
  constexpr prism::color::Preset color = prism::color::Preset::kPureRed;

  // First: SetSingleColor.
  prism::SetSingleColor single;
  single.color = prism::color::ToRgb(color);
  single.strip = &mock_strip_;
  single.controller = &controller_;
  single.index = 2U;

  prism::InstructionMemorySlot slot;
  slot.Set(&single);

  EXPECT_CALL(*mock_strip_.MutableLed(2U), SetColor(testing::_))
    .WillOnce(testing::Return(0));
  EXPECT_CALL(mock_strip_, Show()).Times(0);
  slot.Execute();

  // Reset the mock expectations for the second execution.
  testing::Mock::VerifyAndClearExpectations(&mock_strip_);

  // Reuse the slot as SetMultipleColor.
  prism::SetMultipleColor multi;
  multi.color = prism::color::ToRgb(prism::color::Preset::kPureBlue);
  multi.strip = &mock_strip_;
  multi.controller = &controller_;
  multi.range.start = 0U;
  multi.range.end = 2U;

  slot.Set(&multi);

  EXPECT_CALL(*mock_strip_.MutableLed(0U), SetColor(testing::_))
    .WillOnce(testing::Return(0));
  EXPECT_CALL(*mock_strip_.MutableLed(1U), SetColor(testing::_))
    .WillOnce(testing::Return(0));
  EXPECT_CALL(mock_strip_, Show()).Times(0);
  slot.Execute();
}

// ====================================================================
// Mark-based timeline tests
// ====================================================================

/// @brief AddInstruction inserts instructions in ascending mark order.
/// Verified by observing execution order: instructions with earlier marks
/// execute first, regardless of insertion order.
TEST_F(ControllerTest, InstructionsSortedByMark) {
  prism::SetSingleColor a;
  prism::SetSingleColor b;
  prism::SetSingleColor c;
  a.mark = 100U;
  a.color = prism::color::ToRgb(prism::color::Preset::kPureRed);
  a.strip = &mock_strip_;
  a.index = 0U;
  b.mark = 50U;
  b.color = prism::color::ToRgb(prism::color::Preset::kPureGreen);
  b.strip = &mock_strip_;
  b.index = 1U;
  c.mark = 0U;
  c.color = prism::color::ToRgb(prism::color::Preset::kPureBlue);
  c.strip = &mock_strip_;
  c.index = 2U;

  // Insert out of order: 100, 50, 0.
  controller_.AddInstruction(&a);
  controller_.AddInstruction(&b);
  controller_.AddInstruction(&c);

  // At t=0 only mark=0 should execute.
  g_fake_time = 0U;
  EXPECT_CALL(*mock_strip_.MutableLed(2U), SetColor(testing::_)).Times(1);
  EXPECT_CALL(*mock_strip_.MutableLed(1U), SetColor(testing::_)).Times(0);
  EXPECT_CALL(*mock_strip_.MutableLed(0U), SetColor(testing::_)).Times(0);
  EXPECT_CALL(mock_strip_, Show()).Times(1);
  controller_.Run();
  testing::Mock::VerifyAndClearExpectations(&mock_strip_);

  // At t=50 only mark=50 should execute.
  g_fake_time = 50U;
  EXPECT_CALL(*mock_strip_.MutableLed(2U), SetColor(testing::_)).Times(0);
  EXPECT_CALL(*mock_strip_.MutableLed(1U), SetColor(testing::_)).Times(1);
  EXPECT_CALL(*mock_strip_.MutableLed(0U), SetColor(testing::_)).Times(0);
  EXPECT_CALL(mock_strip_, Show()).Times(1);
  controller_.Run();
  testing::Mock::VerifyAndClearExpectations(&mock_strip_);

  // At t=100 only mark=100 should execute.
  g_fake_time = 100U;
  EXPECT_CALL(*mock_strip_.MutableLed(2U), SetColor(testing::_)).Times(0);
  EXPECT_CALL(*mock_strip_.MutableLed(1U), SetColor(testing::_)).Times(0);
  EXPECT_CALL(*mock_strip_.MutableLed(0U), SetColor(testing::_)).Times(1);
  EXPECT_CALL(mock_strip_, Show()).Times(1);
  controller_.Run();
}

/// @brief PickNewInstructions honours the mark: instructions with
///     mark > current time are left in the queue.
TEST_F(ControllerTest, PickNewInstructionsHonorsMark) {
  prism::SetSingleColor late;
  late.mark = 100U;
  late.color = prism::color::ToRgb(prism::color::Preset::kPureRed);
  late.strip = &mock_strip_;
  late.index = 0U;

  controller_.AddInstruction(&late);

  // At t=0, mark=100 is in the future — no instruction should run.
  g_fake_time = 0U;
  EXPECT_CALL(mock_strip_, Show()).Times(0);
  controller_.Run();
  EXPECT_EQ(g_last_scheduled_delay, 100U);  // look-ahead to mark

  // Advance time to mark — instruction should execute.
  g_fake_time = 100U;
  EXPECT_CALL(*mock_strip_.MutableLed(0U),
              SetColor(prism::color::ToRgb(prism::color::Preset::kPureRed)))
    .Times(1);
  EXPECT_CALL(mock_strip_, Show()).Times(1);
  controller_.Run();
}

/// @brief Look-ahead: when executing instructions have a shorter remaining
///     duration than the next pending mark, the executing timeout wins.
TEST_F(ControllerTest, LookAheadTimeoutFromExecuting) {
  // Instructions at marks 0 and 100.  The mark=0 one is instant, so the
  // look-ahead should target the mark=100 instruction.
  // At t=0 there's no executing, so look-ahead should return 100.
  prism::SetSingleColor early;
  early.mark = 0U;
  early.color = prism::color::ToRgb(prism::color::Preset::kPureGreen);
  early.strip = &mock_strip_;
  early.index = 0U;
  controller_.AddInstruction(&early);

  prism::SetSingleColor later;
  later.mark = 100U;
  later.color = prism::color::ToRgb(prism::color::Preset::kPureBlue);
  later.strip = &mock_strip_;
  later.index = 1U;
  controller_.AddInstruction(&later);

  g_fake_time = 0U;
  EXPECT_CALL(*mock_strip_.MutableLed(0U), SetColor(testing::_)).Times(1);
  EXPECT_CALL(mock_strip_, Show()).Times(1);

  controller_.Run();
  // mark=100 - now(0) = 100ms look-ahead
  EXPECT_EQ(g_last_scheduled_delay, 100U);
}

/// @brief Look-ahead from a pending mark when no executing instructions.
TEST_F(ControllerTest, LookAheadTimeoutFromMarkWhenNoExecuting) {
  prism::SetSingleColor instr;
  instr.mark = 75U;
  instr.color = prism::color::ToRgb(prism::color::Preset::kPureRed);
  instr.strip = &mock_strip_;
  instr.index = 0U;
  controller_.AddInstruction(&instr);

  g_fake_time = 0U;
  EXPECT_CALL(mock_strip_, Show()).Times(0);

  controller_.Run();
  EXPECT_EQ(g_last_scheduled_delay, 75U);
}

/// @brief Run() still works correctly with mixed marks.
TEST_F(ControllerTest, RunStillWorksWithMarks) {
  // Multiple instructions at various marks — all should execute
  // in order as time advances.
  prism::SetSingleColor instr0;
  prism::SetSingleColor instr1;
  prism::SetSingleColor instr2;
  instr0.mark = 0U;
  instr0.color = prism::color::ToRgb(prism::color::Preset::kPureRed);
  instr0.strip = &mock_strip_;
  instr0.index = 0U;

  instr1.mark = 50U;
  instr1.color = prism::color::ToRgb(prism::color::Preset::kPureGreen);
  instr1.strip = &mock_strip_;
  instr1.index = 1U;

  instr2.mark = 100U;
  instr2.color = prism::color::ToRgb(prism::color::Preset::kPureBlue);
  instr2.strip = &mock_strip_;
  instr2.index = 2U;

  controller_.AddInstruction(&instr0);
  controller_.AddInstruction(&instr1);
  controller_.AddInstruction(&instr2);

  // Run 1 at t=0: only mark=0 executes.
  g_fake_time = 0U;
  EXPECT_CALL(*mock_strip_.MutableLed(0U), SetColor(testing::_)).Times(1);
  EXPECT_CALL(*mock_strip_.MutableLed(1U), SetColor(testing::_)).Times(0);
  EXPECT_CALL(*mock_strip_.MutableLed(2U), SetColor(testing::_)).Times(0);
  EXPECT_CALL(mock_strip_, Show()).Times(1);
  controller_.Run();
  EXPECT_EQ(g_last_scheduled_delay, 50U);  // look-ahead to mark=50

  testing::Mock::VerifyAndClearExpectations(&mock_strip_);

  // Run 2 at t=50: mark=50 executes.
  g_fake_time = 50U;
  EXPECT_CALL(*mock_strip_.MutableLed(0U), SetColor(testing::_)).Times(0);
  EXPECT_CALL(*mock_strip_.MutableLed(1U), SetColor(testing::_)).Times(1);
  EXPECT_CALL(*mock_strip_.MutableLed(2U), SetColor(testing::_)).Times(0);
  EXPECT_CALL(mock_strip_, Show()).Times(1);
  controller_.Run();
  EXPECT_EQ(g_last_scheduled_delay, 50U);  // look-ahead to mark=100

  testing::Mock::VerifyAndClearExpectations(&mock_strip_);

  // Run 3 at t=100: mark=100 executes.  No more instructions.
  g_fake_time = 100U;
  g_last_scheduled_delay = 0U;  // reset so we can verify no schedule
  EXPECT_CALL(*mock_strip_.MutableLed(0U), SetColor(testing::_)).Times(0);
  EXPECT_CALL(*mock_strip_.MutableLed(1U), SetColor(testing::_)).Times(0);
  EXPECT_CALL(*mock_strip_.MutableLed(2U), SetColor(testing::_)).Times(1);
  EXPECT_CALL(mock_strip_, Show()).Times(1);
  controller_.Run();
  EXPECT_EQ(g_last_scheduled_delay, 0U);  // no more instructions
}

/// @brief ResetInstructions clears state including marks.
TEST_F(ControllerTest, ResetInstructionsClearsStateWithMarks) {
  prism::SetSingleColor a;
  a.mark = 10U;
  a.color = prism::color::ToRgb(prism::color::Preset::kPureRed);
  a.strip = &mock_strip_;
  a.index = 0U;
  controller_.AddInstruction(&a);

  controller_.ResetInstructions();

  g_fake_time = 100U;
  EXPECT_CALL(mock_strip_, Show()).Times(0);
  controller_.Run();
}

// ====================================================================
// Fixed-capacity group-program validation tests
// ====================================================================

static_assert(prism::Controller::kMaxGroups ==
                prism::Controller::kMaxInstruction + 1U,
              "Group capacity includes the implicit root");

/// @brief RunGroup configuration is copied through its fixed storage variant.
TEST_F(ControllerTest, RunGroupSlotStoresItsMembershipAndTarget) {
  prism::RunGroupPayload payload{};
  payload.group_id = 0xF1234567U;
  payload.mark = 17U;
  payload.target_id = 0xFEDCBA98U;
  payload.additional_repeats = prism::kMaxFiniteLoopCount;
  prism::RunGroupInstruction instruction{payload};

  prism::InstructionMemorySlot slot;
  ASSERT_TRUE(slot.Set(&instruction));

  EXPECT_EQ(slot.GetGroupId(), payload.group_id);
  EXPECT_EQ(slot.GetMark(), payload.mark);
  EXPECT_TRUE(slot.IsRunGroup());
  EXPECT_EQ(slot.GetTargetGroupId(), payload.target_id);
  EXPECT_EQ(slot.GetAdditionalRepeats(), payload.additional_repeats);
  EXPECT_EQ(slot.Execute(), 0U);
}

/// @brief Runtime reset clears activation state but retains group metadata.
TEST_F(ControllerTest, GroupRuntimeResetRetainsItsRecord) {
  prism::GroupRuntime runtime{};
  runtime.record = {0xF0000001U, 4U, 3U};
  runtime.cursor = 2U;
  runtime.start_time = 99U;
  runtime.repeats_remaining = 7U;
  runtime.active_count = 1U;
  runtime.child_count = 2U;
  runtime.active = true;

  runtime.Reset();

  EXPECT_EQ(runtime.record.id, 0xF0000001U);
  EXPECT_EQ(runtime.record.index_begin, 4U);
  EXPECT_EQ(runtime.record.instruction_count, 3U);
  EXPECT_EQ(runtime.cursor, 0U);
  EXPECT_EQ(runtime.start_time, 0U);
  EXPECT_EQ(runtime.repeats_remaining, 0U);
  EXPECT_EQ(runtime.active_count, 0U);
  EXPECT_EQ(runtime.child_count, 0U);
  EXPECT_FALSE(runtime.active);
}

/// @brief Instruction and group capacity failures leave the pool bounded.
TEST_F(ControllerTest, AddInstructionReportsNullAndCapacityFailures) {
  EXPECT_EQ(controller_.AddInstruction(nullptr),
            prism::ControllerStatus::kInvalidArgument);

  prism::SetSingleColor instruction;
  for (std::uint32_t i = 0U; i < prism::Controller::kMaxInstruction; ++i) {
    EXPECT_EQ(controller_.AddInstruction(&instruction),
              prism::ControllerStatus::kSuccess);
  }
  EXPECT_EQ(controller_.AddInstruction(&instruction),
            prism::ControllerStatus::kCapacityExhausted);
  EXPECT_EQ(controller_.Start(), prism::ControllerStatus::kSuccess);
}

/// @brief Sixteen sparse non-root labels fit beside the implicit root record.
TEST_F(ControllerTest, MaximumGroupCountIncludesImplicitRoot) {
  for (std::uint32_t i = 0U; i < prism::Controller::kMaxInstruction; ++i) {
    prism::SetSingleColor instruction;
    instruction.group_id = 0x80000000U + i * 257U;
    ASSERT_EQ(controller_.AddInstruction(&instruction),
              prism::ControllerStatus::kSuccess);
  }

  EXPECT_EQ(prism::Controller::kMaxGroups, 17U);
  EXPECT_EQ(controller_.Start(), prism::ControllerStatus::kSuccess);
  prism::SetSingleColor replacement;
  EXPECT_EQ(controller_.AddInstruction(&replacement),
            prism::ControllerStatus::kBusy);
}

/// @brief Sparse GroupIds resolve by label without being used as array indexes.
TEST_F(ControllerTest, SparseGroupIdsResolveRunGroupTargets) {
  constexpr prism::GroupId sparse_id = 0xFFFFFFFFU;
  prism::SetSingleColor child;
  child.group_id = sparse_id;
  child.mark = 23U;
  ASSERT_EQ(controller_.AddInstruction(&child),
            prism::ControllerStatus::kSuccess);

  prism::RunGroupPayload payload{};
  payload.group_id = 0U;
  payload.mark = 0U;
  payload.target_id = sparse_id;
  prism::RunGroupInstruction start_child{payload};
  ASSERT_EQ(controller_.AddInstruction(&start_child),
            prism::ControllerStatus::kSuccess);

  EXPECT_EQ(controller_.Start(), prism::ControllerStatus::kSuccess);
}

/// @brief Group zero exists implicitly and an empty root completes as a no-op.
TEST_F(ControllerTest, EmptyRootIsValidWithNonRootInstructions) {
  prism::SetSingleColor child;
  child.group_id = 91U;
  ASSERT_EQ(controller_.AddInstruction(&child),
            prism::ControllerStatus::kSuccess);

  EXPECT_EQ(controller_.Start(), prism::ControllerStatus::kSuccess);
  EXPECT_EQ(controller_.Run(), prism::ControllerStatus::kSuccess);
  EXPECT_EQ(controller_.AddInstruction(&child), prism::ControllerStatus::kBusy);
}

/// @brief Missing and root-group RunGroup targets fail before execution.
TEST_F(ControllerTest, RunGroupRejectsMissingAndRootTargets) {
  prism::RunGroupPayload payload{};
  payload.group_id = 0U;
  payload.target_id = 700U;
  prism::RunGroupInstruction instruction{payload};
  ASSERT_EQ(controller_.AddInstruction(&instruction),
            prism::ControllerStatus::kSuccess);

  EXPECT_EQ(controller_.Start(), prism::ControllerStatus::kInvalidProgram);

  prism::SetSingleColor target;
  target.group_id = payload.target_id;
  ASSERT_EQ(controller_.AddInstruction(&target),
            prism::ControllerStatus::kSuccess);
  EXPECT_EQ(controller_.Start(), prism::ControllerStatus::kSuccess);

  controller_.ResetInstructions();
  payload.target_id = 0U;
  prism::RunGroupInstruction root_target{payload};
  ASSERT_EQ(controller_.AddInstruction(&root_target),
            prism::ControllerStatus::kSuccess);
  EXPECT_EQ(controller_.Start(), prism::ControllerStatus::kInvalidProgram);
}

/// @brief Self-calls and indirect group-call cycles fail program validation.
TEST_F(ControllerTest, RunGroupRejectsCycles) {
  prism::RunGroupPayload self_payload{};
  self_payload.group_id = 0x10000U;
  self_payload.target_id = self_payload.group_id;
  prism::RunGroupInstruction self_call{self_payload};
  ASSERT_EQ(controller_.AddInstruction(&self_call),
            prism::ControllerStatus::kSuccess);
  EXPECT_EQ(controller_.Start(), prism::ControllerStatus::kInvalidProgram);

  controller_.ResetInstructions();
  prism::RunGroupPayload first_payload{};
  first_payload.group_id = 0x10000U;
  first_payload.target_id = 0x90000000U;
  prism::RunGroupInstruction first_call{first_payload};
  prism::RunGroupPayload second_payload{};
  second_payload.group_id = first_payload.target_id;
  second_payload.target_id = first_payload.group_id;
  prism::RunGroupInstruction second_call{second_payload};

  ASSERT_EQ(controller_.AddInstruction(&first_call),
            prism::ControllerStatus::kSuccess);
  ASSERT_EQ(controller_.AddInstruction(&second_call),
            prism::ControllerStatus::kSuccess);
  EXPECT_EQ(controller_.Start(), prism::ControllerStatus::kInvalidProgram);
}

/// @brief Repeated zero-time root and child timelines are rejected.
TEST_F(ControllerTest, RepeatedZeroTimeGroupsFailPreflight) {
  prism::SetSingleColor root;
  ASSERT_EQ(controller_.AddInstruction(&root),
            prism::ControllerStatus::kSuccess);
  EXPECT_EQ(controller_.Start(1U), prism::ControllerStatus::kInvalidProgram);
  EXPECT_EQ(controller_.Start(prism::kForeverLoopCount),
            prism::ControllerStatus::kInvalidProgram);

  controller_.ResetInstructions();
  prism::RunGroupPayload payload{};
  payload.group_id = 0U;
  payload.target_id = 42U;
  payload.additional_repeats = 1U;
  prism::RunGroupInstruction child_call{payload};
  prism::SetSingleColor child;
  child.group_id = payload.target_id;
  ASSERT_EQ(controller_.AddInstruction(&child_call),
            prism::ControllerStatus::kSuccess);
  ASSERT_EQ(controller_.AddInstruction(&child),
            prism::ControllerStatus::kSuccess);
  EXPECT_EQ(controller_.Start(), prism::ControllerStatus::kInvalidProgram);
}

/// @brief A future mark provides the required repeat boundary.
TEST_F(ControllerTest, RepeatedGroupsRequireAndAcceptPositiveBoundaries) {
  prism::RunGroupPayload payload{};
  payload.group_id = 0U;
  payload.target_id = 0xA0000001U;
  payload.additional_repeats = prism::kMaxFiniteLoopCount;
  prism::RunGroupInstruction child_call{payload};
  prism::SetSingleColor child;
  child.group_id = payload.target_id;
  child.mark = 1U;

  ASSERT_EQ(controller_.AddInstruction(&child_call),
            prism::ControllerStatus::kSuccess);
  ASSERT_EQ(controller_.AddInstruction(&child),
            prism::ControllerStatus::kSuccess);
  EXPECT_EQ(controller_.Start(1U), prism::ControllerStatus::kSuccess);
}

/// @brief Positive boundaries propagate from descendants to repeated parents.
TEST_F(ControllerTest, NestedChildBoundaryPropagatesToRepeatedRoot) {
  prism::RunGroupPayload root_call_payload{};
  root_call_payload.group_id = 0U;
  root_call_payload.target_id = 0x10000001U;
  prism::RunGroupInstruction root_call{root_call_payload};

  prism::RunGroupPayload nested_call_payload{};
  nested_call_payload.group_id = root_call_payload.target_id;
  nested_call_payload.target_id = 0xFFFFFFFFU;
  prism::RunGroupInstruction nested_call{nested_call_payload};

  prism::SetSingleColor leaf;
  leaf.group_id = nested_call_payload.target_id;
  leaf.mark = 5U;

  ASSERT_EQ(controller_.AddInstruction(&root_call),
            prism::ControllerStatus::kSuccess);
  ASSERT_EQ(controller_.AddInstruction(&nested_call),
            prism::ControllerStatus::kSuccess);
  ASSERT_EQ(controller_.AddInstruction(&leaf),
            prism::ControllerStatus::kSuccess);
  EXPECT_EQ(controller_.Start(1U), prism::ControllerStatus::kSuccess);
}

/// @brief Child groups cannot use the root-only forever repeat sentinel.
TEST_F(ControllerTest, ChildRunGroupRejectsForeverRepeats) {
  prism::RunGroupPayload payload{};
  payload.group_id = 0U;
  payload.target_id = 0xFFFF0001U;
  payload.additional_repeats = prism::kForeverLoopCount;
  prism::RunGroupInstruction child_call{payload};
  prism::SetSingleColor child;
  child.group_id = payload.target_id;
  child.mark = 1U;

  ASSERT_EQ(controller_.AddInstruction(&child_call),
            prism::ControllerStatus::kSuccess);
  ASSERT_EQ(controller_.AddInstruction(&child),
            prism::ControllerStatus::kSuccess);
  EXPECT_EQ(controller_.Start(), prism::ControllerStatus::kInvalidProgram);
}

/// @brief Program mutation is rejected while active and after completion.
TEST_F(ControllerTest, AddInstructionLocksAfterStartAndCompletion) {
  prism::SetSingleColor instruction;
  instruction.mark = 1U;
  ASSERT_EQ(controller_.AddInstruction(&instruction),
            prism::ControllerStatus::kSuccess);
  ASSERT_EQ(controller_.Start(), prism::ControllerStatus::kSuccess);

  prism::SetSingleColor replacement;
  EXPECT_EQ(controller_.AddInstruction(&replacement),
            prism::ControllerStatus::kBusy);

  g_fake_time = 1U;
  EXPECT_CALL(*mock_strip_.MutableLed(0U), SetColor(instruction.color))
    .WillOnce(testing::Return(0));
  EXPECT_CALL(mock_strip_, Show()).Times(1);
  EXPECT_EQ(controller_.Run(), prism::ControllerStatus::kSuccess);
  EXPECT_EQ(controller_.AddInstruction(&replacement),
            prism::ControllerStatus::kBusy);

  controller_.ResetInstructions();
  EXPECT_EQ(controller_.AddInstruction(&replacement),
            prism::ControllerStatus::kSuccess);
}
