/// @file Tests for prism::Controller and its instruction classes.
///
/// These are host-compiled unit tests (no Zephyr dependencies).  The
/// Controller implementation lives in app/controller/.

#include "prism/controller.hpp"

#include <array>
#include <limits>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mock_strip.hpp"
#include "oklab.hpp"
#include "prism/color.hpp"
#include "prism/gradient.hpp"
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
/// @brief Number of reads made through the incrementing timestamp callback.
std::uint32_t g_timestamp_read_count = 0U;

/// @brief Returns g_fake_time so tests can control the clock.
std::uint32_t FakeTimestamp() { return g_fake_time; }
/// @brief Returns and increments g_fake_time while counting clock reads.
std::uint32_t IncrementingTimestamp() {
  ++g_timestamp_read_count;
  return g_fake_time++;
}

class ControllerTest : public ::testing::Test {
 protected:
  void SetUp() override {
    g_fake_time = 0U;
    g_timestamp_read_count = 0U;
    controller_.SetStrip(&mock_strip_);
    controller_.SetTimestampCallback(FakeTimestamp);
    controller_.SetScheduleCallback(OnScheduleNextRun);
    g_last_scheduled_delay = 0U;
  }

  prism::test::MockStrip mock_strip_;
  prism::Controller controller_;
};

void AddFade(prism::Controller& controller, std::uint8_t index,
             std::uint16_t percent, std::uint16_t duration_ms,
             prism::Mark mark = 0U) {
  prism::FadePayload payload{};
  payload.mark = mark;
  payload.percent = percent;
  payload.duration_ms = duration_ms;
  payload.index = index;
  prism::Fade fade{payload};
  EXPECT_EQ(controller.AddInstruction(&fade),
            prism::ControllerStatus::kSuccess);
}

void AddGradient(prism::Controller& controller, std::uint8_t index,
                 prism::color::RgbColor target, std::uint16_t duration_ms,
                 prism::Mark mark = 0U, prism::GroupId group_id = 0U) {
  prism::GradientPayload payload{};
  payload.mark = mark;
  payload.r = target.red;
  payload.g = target.green;
  payload.b = target.blue;
  payload.duration_ms = duration_ms;
  payload.index = index;
  payload.group_id = group_id;
  prism::Gradient gradient{payload};
  EXPECT_EQ(controller.AddInstruction(&gradient),
            prism::ControllerStatus::kSuccess);
}

// ====================================================================
// Fade instruction tests
// ====================================================================

TEST_F(ControllerTest, FadeCapturesActivationColorInterpolatesAndFinishesLate) {
  constexpr std::uint8_t index = 2U;
  constexpr prism::color::RgbColor start{180U, 90U, 45U};
  constexpr prism::color::RgbColor halfway{112U, 56U, 28U};
  constexpr prism::color::RgbColor target{45U, 23U, 11U};
  auto* led = mock_strip_.MutableLed(index);

  AddFade(controller_, index, 25U, 100U);
  ASSERT_EQ(controller_.Start(), prism::ControllerStatus::kSuccess);

  // Capture the staged RGB only when the instruction becomes due.
  EXPECT_CALL(*led, Color()).WillOnce(testing::Return(start));
  EXPECT_CALL(mock_strip_, Show()).Times(0);
  EXPECT_EQ(controller_.Run(), prism::ControllerStatus::kSuccess);
  EXPECT_EQ(g_last_scheduled_delay, 20U);

  g_fake_time = 50U;
  g_last_scheduled_delay = 0U;
  EXPECT_CALL(*led, SetColor(halfway)).WillOnce(testing::Return(0));
  EXPECT_CALL(*mock_strip_.MutableLed(0U), SetColor(testing::_)).Times(0);
  EXPECT_CALL(*mock_strip_.MutableLed(1U), SetColor(testing::_)).Times(0);
  EXPECT_CALL(*mock_strip_.MutableLed(3U), SetColor(testing::_)).Times(0);
  EXPECT_CALL(*mock_strip_.MutableLed(4U), SetColor(testing::_)).Times(0);
  EXPECT_CALL(*mock_strip_.MutableLed(5U), SetColor(testing::_)).Times(0);
  EXPECT_CALL(*mock_strip_.MutableLed(6U), SetColor(testing::_)).Times(0);
  EXPECT_CALL(*mock_strip_.MutableLed(7U), SetColor(testing::_)).Times(0);
  EXPECT_CALL(mock_strip_, Show()).Times(1);
  EXPECT_EQ(controller_.Run(), prism::ControllerStatus::kSuccess);
  EXPECT_EQ(g_last_scheduled_delay, 10U);

  // A late callback still completes at the exact target from elapsed time.
  g_fake_time = 150U;
  g_last_scheduled_delay = 0U;
  EXPECT_CALL(*led, SetColor(target)).WillOnce(testing::Return(0));
  EXPECT_CALL(mock_strip_, Show()).Times(1);
  EXPECT_EQ(controller_.Run(), prism::ControllerStatus::kSuccess);
  EXPECT_EQ(g_last_scheduled_delay, 0U);
}

TEST_F(ControllerTest, Fade400PercentSaturatesValueAt255) {
  constexpr std::uint8_t index = 1U;
  constexpr prism::color::RgbColor start{180U, 90U, 45U};
  constexpr prism::color::RgbColor target{255U, 128U, 64U};
  auto* led = mock_strip_.MutableLed(index);

  AddFade(controller_, index, 400U, 0U);
  ASSERT_EQ(controller_.Start(), prism::ControllerStatus::kSuccess);
  EXPECT_CALL(*led, Color()).WillOnce(testing::Return(start));
  EXPECT_CALL(*led, SetColor(target)).WillOnce(testing::Return(0));
  EXPECT_CALL(mock_strip_, Show()).Times(1);

  EXPECT_EQ(controller_.Run(), prism::ControllerStatus::kSuccess);
  EXPECT_EQ(g_last_scheduled_delay, 0U);
}

TEST_F(ControllerTest, FadeZeroDurationAppliesTargetImmediately) {
  constexpr std::uint8_t index = 0U;
  constexpr prism::color::RgbColor start{200U, 100U, 0U};
  constexpr prism::color::RgbColor target{50U, 25U, 0U};
  auto* led = mock_strip_.MutableLed(index);

  AddFade(controller_, index, 25U, 0U);
  ASSERT_EQ(controller_.Start(), prism::ControllerStatus::kSuccess);
  EXPECT_CALL(*led, Color()).WillOnce(testing::Return(start));
  EXPECT_CALL(*led, SetColor(target)).WillOnce(testing::Return(0));
  EXPECT_CALL(mock_strip_, Show()).Times(1);

  EXPECT_EQ(controller_.Run(), prism::ControllerStatus::kSuccess);
  EXPECT_EQ(g_last_scheduled_delay, 0U);
}

TEST_F(ControllerTest, FadeBlackInputRemainsBlackWhileFadingUp) {
  constexpr std::uint8_t index = 4U;
  constexpr prism::color::RgbColor black{0U, 0U, 0U};
  auto* led = mock_strip_.MutableLed(index);

  AddFade(controller_, index, 400U, 40U);
  ASSERT_EQ(controller_.Start(), prism::ControllerStatus::kSuccess);
  EXPECT_CALL(*led, Color()).WillOnce(testing::Return(black));
  EXPECT_CALL(mock_strip_, Show()).Times(0);
  EXPECT_EQ(controller_.Run(), prism::ControllerStatus::kSuccess);
  EXPECT_EQ(g_last_scheduled_delay, 20U);

  g_fake_time = 20U;
  g_last_scheduled_delay = 0U;
  EXPECT_CALL(*led, SetColor(black)).WillOnce(testing::Return(0));
  EXPECT_CALL(mock_strip_, Show()).Times(1);
  EXPECT_EQ(controller_.Run(), prism::ControllerStatus::kSuccess);
  EXPECT_EQ(g_last_scheduled_delay, 20U);

  g_fake_time = 40U;
  g_last_scheduled_delay = 0U;
  EXPECT_CALL(*led, SetColor(black)).WillOnce(testing::Return(0));
  EXPECT_CALL(mock_strip_, Show()).Times(1);
  EXPECT_EQ(controller_.Run(), prism::ControllerStatus::kSuccess);
  EXPECT_EQ(g_last_scheduled_delay, 0U);
}

TEST_F(ControllerTest, FadeZeroDurationDoesNotPermitRepeatedRootGroup) {
  AddFade(controller_, 0U, 400U, 0U);

  EXPECT_EQ(controller_.Start(1U), prism::ControllerStatus::kInvalidProgram);
}

TEST_F(ControllerTest, FadeReplayCapturesNewStagedColor) {
  constexpr std::uint8_t index = 0U;
  constexpr prism::color::RgbColor initial{100U, 50U, 25U};
  constexpr prism::color::RgbColor first_target{25U, 13U, 6U};
  constexpr prism::color::RgbColor replay_target{6U, 3U, 1U};
  auto* led = mock_strip_.MutableLed(index);

  AddFade(controller_, index, 25U, 100U);
  ASSERT_EQ(controller_.Start(), prism::ControllerStatus::kSuccess);
  EXPECT_CALL(*led, Color()).WillOnce(testing::Return(initial));
  EXPECT_CALL(mock_strip_, Show()).Times(0);
  EXPECT_EQ(controller_.Run(), prism::ControllerStatus::kSuccess);

  g_fake_time = 100U;
  g_last_scheduled_delay = 0U;
  EXPECT_CALL(*led, SetColor(first_target)).WillOnce(testing::Return(0));
  EXPECT_CALL(mock_strip_, Show()).Times(1);
  EXPECT_EQ(controller_.Run(), prism::ControllerStatus::kSuccess);
  EXPECT_EQ(g_last_scheduled_delay, 0U);

  ASSERT_EQ(controller_.Start(), prism::ControllerStatus::kSuccess);
  EXPECT_CALL(*led, Color()).WillOnce(testing::Return(first_target));
  EXPECT_CALL(mock_strip_, Show()).Times(0);
  EXPECT_EQ(controller_.Run(), prism::ControllerStatus::kSuccess);
  EXPECT_EQ(g_last_scheduled_delay, 20U);

  g_fake_time = 200U;
  g_last_scheduled_delay = 0U;
  EXPECT_CALL(*led, SetColor(replay_target)).WillOnce(testing::Return(0));
  EXPECT_CALL(mock_strip_, Show()).Times(1);
  EXPECT_EQ(controller_.Run(), prism::ControllerStatus::kSuccess);
  EXPECT_EQ(g_last_scheduled_delay, 0U);
}

TEST_F(ControllerTest, FadeUpdatesUseSharedTwentyMillisecondGrid) {
  constexpr prism::color::RgbColor start{100U, 0U, 0U};
  constexpr prism::color::RgbColor firstAtTwentyMs{80U, 0U, 0U};
  constexpr prism::color::RgbColor secondAtTwentyMs{87U, 0U, 0U};
  auto* first_led = mock_strip_.MutableLed(0U);
  auto* second_led = mock_strip_.MutableLed(1U);

  AddFade(controller_, 0U, 0U, 100U);
  AddFade(controller_, 1U, 0U, 100U, 7U);
  ASSERT_EQ(controller_.Start(), prism::ControllerStatus::kSuccess);
  EXPECT_CALL(*first_led, Color()).WillOnce(testing::Return(start));
  EXPECT_CALL(mock_strip_, Show()).Times(0);
  EXPECT_EQ(controller_.Run(), prism::ControllerStatus::kSuccess);
  EXPECT_EQ(g_last_scheduled_delay, 7U);

  g_fake_time = 7U;
  g_last_scheduled_delay = 0U;
  EXPECT_CALL(*second_led, Color()).WillOnce(testing::Return(start));
  EXPECT_CALL(mock_strip_, Show()).Times(0);
  EXPECT_EQ(controller_.Run(), prism::ControllerStatus::kSuccess);
  EXPECT_EQ(g_last_scheduled_delay, 13U);

  g_fake_time = 20U;
  g_last_scheduled_delay = 0U;
  EXPECT_CALL(*first_led, SetColor(firstAtTwentyMs))
    .WillOnce(testing::Return(0));
  EXPECT_CALL(*second_led, SetColor(secondAtTwentyMs))
    .WillOnce(testing::Return(0));
  EXPECT_CALL(mock_strip_, Show()).Times(1);
  EXPECT_EQ(controller_.Run(), prism::ControllerStatus::kSuccess);
  EXPECT_EQ(g_last_scheduled_delay, 20U);
}

/// @brief Goal: validate refresh-period defaults, bounds, and lifecycle rules.
/// @details Steps: change the period while editable, reject zero and half-range
///     values, reject changes while active, then verify reset and completed
///     programs preserve or accept configuration as specified.
TEST_F(ControllerTest, RefreshPeriodValidatesAndPersistsAcrossReset) {
  EXPECT_EQ(controller_.RefreshPeriodMs(), 20U);
  EXPECT_EQ(controller_.SetRefreshPeriodMs(15U),
            prism::ControllerStatus::kSuccess);
  EXPECT_EQ(controller_.RefreshPeriodMs(), 15U);

  EXPECT_EQ(controller_.SetRefreshPeriodMs(0U),
            prism::ControllerStatus::kInvalidArgument);
  EXPECT_EQ(controller_.SetRefreshPeriodMs(0x80000000U),
            prism::ControllerStatus::kInvalidArgument);
  EXPECT_EQ(
    controller_.SetRefreshPeriodMs(std::numeric_limits<std::uint32_t>::max()),
    prism::ControllerStatus::kInvalidArgument);
  EXPECT_EQ(controller_.RefreshPeriodMs(), 15U);

  AddFade(controller_, 0U, 0U, 20U);
  ASSERT_EQ(controller_.Start(), prism::ControllerStatus::kSuccess);
  EXPECT_EQ(controller_.SetRefreshPeriodMs(25U),
            prism::ControllerStatus::kBusy);
  EXPECT_EQ(controller_.RefreshPeriodMs(), 15U);

  controller_.ResetInstructions();
  EXPECT_EQ(controller_.RefreshPeriodMs(), 15U);
  ASSERT_EQ(controller_.Start(), prism::ControllerStatus::kSuccess);
  EXPECT_EQ(controller_.SetRefreshPeriodMs(25U),
            prism::ControllerStatus::kSuccess);
  EXPECT_EQ(controller_.RefreshPeriodMs(), 25U);
  EXPECT_EQ(controller_.SetRefreshPeriodMs(0x7FFFFFFFU),
            prism::ControllerStatus::kSuccess);
  EXPECT_EQ(controller_.RefreshPeriodMs(), 0x7FFFFFFFU);
}

/// @brief Goal: prove Fade consumes a non-default period on its absolute grid.
/// @details Steps: activate at t=7 with a 15 ms period, verify the first wake
///     at t=15, then complete and replay to confirm the period remains active.
TEST_F(ControllerTest, FadeUsesConfiguredPeriodAndRetainsItOnReplay) {
  constexpr std::uint8_t index = 0U;
  constexpr prism::color::RgbColor source{100U, 0U, 0U};
  constexpr prism::color::RgbColor first_sample{87U, 0U, 0U};
  constexpr prism::color::RgbColor target{0U, 0U, 0U};
  constexpr std::uint16_t duration_ms = 60U;
  auto* led = mock_strip_.MutableLed(index);

  ASSERT_EQ(controller_.SetRefreshPeriodMs(15U),
            prism::ControllerStatus::kSuccess);
  AddFade(controller_, index, 0U, duration_ms);
  g_fake_time = 7U;
  ASSERT_EQ(controller_.Start(), prism::ControllerStatus::kSuccess);
  EXPECT_CALL(*led, Color()).WillOnce(testing::Return(source));
  EXPECT_CALL(mock_strip_, Show()).Times(0);
  EXPECT_EQ(controller_.Run(), prism::ControllerStatus::kSuccess);
  EXPECT_EQ(g_last_scheduled_delay, 8U);

  g_fake_time = 15U;
  g_last_scheduled_delay = 0U;
  EXPECT_CALL(*led, SetColor(first_sample)).WillOnce(testing::Return(0));
  EXPECT_CALL(mock_strip_, Show()).Times(1);
  EXPECT_EQ(controller_.Run(), prism::ControllerStatus::kSuccess);
  EXPECT_EQ(g_last_scheduled_delay, 15U);

  g_fake_time = 67U;
  g_last_scheduled_delay = 0U;
  EXPECT_CALL(*led, SetColor(target)).WillOnce(testing::Return(0));
  EXPECT_CALL(mock_strip_, Show()).Times(1);
  EXPECT_EQ(controller_.Run(), prism::ControllerStatus::kSuccess);
  EXPECT_EQ(g_last_scheduled_delay, 0U);

  ASSERT_EQ(controller_.Start(), prism::ControllerStatus::kSuccess);
  EXPECT_CALL(*led, Color()).WillOnce(testing::Return(target));
  EXPECT_CALL(mock_strip_, Show()).Times(0);
  EXPECT_EQ(controller_.Run(), prism::ControllerStatus::kSuccess);
  EXPECT_EQ(g_last_scheduled_delay, 8U);
  EXPECT_EQ(controller_.RefreshPeriodMs(), 15U);
}

/// @brief Goal: prove Gradient consumes the configured period relative to
/// start.
/// @details Steps: activate at t=7 with a 15 ms period, then verify the first
///     interpolation sample and next wake occur at elapsed 15 ms.
TEST_F(ControllerTest, GradientUsesConfiguredStartRelativePeriod) {
  constexpr std::uint8_t index = 2U;
  constexpr prism::color::RgbColor source{40U, 100U, 180U};
  constexpr prism::color::RgbColor target{220U, 50U, 10U};
  constexpr std::uint16_t duration_ms = 60U;
  auto* led = mock_strip_.MutableLed(index);

  ASSERT_EQ(controller_.SetRefreshPeriodMs(15U),
            prism::ControllerStatus::kSuccess);
  AddGradient(controller_, index, target, duration_ms);
  g_fake_time = 7U;
  ASSERT_EQ(controller_.Start(), prism::ControllerStatus::kSuccess);
  EXPECT_CALL(*led, Color()).WillOnce(testing::Return(source));
  EXPECT_CALL(mock_strip_, Show()).Times(0);
  EXPECT_EQ(controller_.Run(), prism::ControllerStatus::kSuccess);
  EXPECT_EQ(g_last_scheduled_delay, 15U);

  g_fake_time = 22U;
  g_last_scheduled_delay = 0U;
  const auto sample =
    prism::controller_detail::InterpolateRgb(source, target, 15U, duration_ms);
  EXPECT_CALL(*led, SetColor(sample)).WillOnce(testing::Return(0));
  EXPECT_CALL(mock_strip_, Show()).Times(1);
  EXPECT_EQ(controller_.Run(), prism::ControllerStatus::kSuccess);
  EXPECT_EQ(g_last_scheduled_delay, 15U);
}

TEST_F(ControllerTest, FadeElapsedTimeIsWrapSafe) {
  constexpr std::uint8_t index = 0U;
  constexpr std::uint32_t start_time =
    std::numeric_limits<std::uint32_t>::max() - 15U;
  constexpr prism::color::RgbColor start{100U, 0U, 0U};
  constexpr prism::color::RgbColor halfway{50U, 0U, 0U};
  constexpr prism::color::RgbColor target{0U, 0U, 0U};
  auto* led = mock_strip_.MutableLed(index);

  AddFade(controller_, index, 0U, 40U);
  g_fake_time = start_time;
  ASSERT_EQ(controller_.Start(), prism::ControllerStatus::kSuccess);
  EXPECT_CALL(*led, Color()).WillOnce(testing::Return(start));
  EXPECT_CALL(mock_strip_, Show()).Times(0);
  EXPECT_EQ(controller_.Run(), prism::ControllerStatus::kSuccess);
  EXPECT_EQ(g_last_scheduled_delay, 20U);

  g_fake_time = 4U;
  g_last_scheduled_delay = 0U;
  EXPECT_CALL(*led, SetColor(halfway)).WillOnce(testing::Return(0));
  EXPECT_CALL(mock_strip_, Show()).Times(1);
  EXPECT_EQ(controller_.Run(), prism::ControllerStatus::kSuccess);
  EXPECT_EQ(g_last_scheduled_delay, 16U);

  g_fake_time = 24U;
  g_last_scheduled_delay = 0U;
  EXPECT_CALL(*led, SetColor(target)).WillOnce(testing::Return(0));
  EXPECT_CALL(mock_strip_, Show()).Times(1);
  EXPECT_EQ(controller_.Run(), prism::ControllerStatus::kSuccess);
  EXPECT_EQ(g_last_scheduled_delay, 0U);
}

/// @brief Goal: keep Fade grid alignment and deadlines on one time snapshot.
/// @details Steps: advance the clock on each read, activate two Fades, then
/// verify Run uses one read and schedules from that shared snapshot.
TEST_F(ControllerTest, FadesUseSingleRunTimestampForActivationAndScheduling) {
  constexpr prism::color::RgbColor start{100U, 0U, 0U};
  AddFade(controller_, 0U, 0U, 100U);
  AddFade(controller_, 1U, 0U, 100U);
  g_fake_time = 100U;
  controller_.SetTimestampCallback(IncrementingTimestamp);
  ASSERT_EQ(controller_.Start(), prism::ControllerStatus::kSuccess);
  const std::uint32_t reads_after_start = g_timestamp_read_count;

  EXPECT_CALL(*mock_strip_.MutableLed(0U), Color())
    .WillOnce(testing::Return(start));
  EXPECT_CALL(*mock_strip_.MutableLed(1U), Color())
    .WillOnce(testing::Return(start));
  EXPECT_CALL(mock_strip_, Show()).Times(0);
  EXPECT_EQ(controller_.Run(), prism::ControllerStatus::kSuccess);

  EXPECT_EQ(g_timestamp_read_count, reads_after_start + 1U);
  EXPECT_EQ(g_last_scheduled_delay, 19U);
}

TEST_F(ControllerTest,
       FadeTwentyFiveThenFourHundredPercentCharacterizesPalette) {
  constexpr std::array<prism::color::RgbColor, 7U> palette{{
    {255U, 0U, 0U},
    {255U, 40U, 0U},
    {180U, 60U, 0U},
    {0U, 80U, 0U},
    {0U, 0U, 255U},
    {40U, 0U, 160U},
    {140U, 0U, 255U},
  }};
  constexpr std::array<prism::color::RgbColor, 7U> dimmed{{
    {64U, 0U, 0U},
    {64U, 10U, 0U},
    {45U, 15U, 0U},
    {0U, 20U, 0U},
    {0U, 0U, 64U},
    {10U, 0U, 40U},
    {35U, 0U, 64U},
  }};
  constexpr std::array<prism::color::RgbColor, 7U> restored{{
    {255U, 0U, 0U},
    {255U, 40U, 0U},
    {180U, 60U, 0U},
    {0U, 80U, 0U},
    {0U, 0U, 255U},
    {40U, 0U, 160U},
    {139U, 0U, 255U},
  }};

  for (std::uint8_t index = 0U; index < palette.size(); ++index) {
    AddFade(controller_, index, 25U, 0U);
    AddFade(controller_, index, 400U, 0U);

    auto* led = mock_strip_.MutableLed(index);
    EXPECT_CALL(*led, Color())
      .WillOnce(testing::Return(palette[index]))
      .WillOnce(testing::Return(dimmed[index]));
    EXPECT_CALL(*led, SetColor(dimmed[index])).WillOnce(testing::Return(0));
    EXPECT_CALL(*led, SetColor(restored[index])).WillOnce(testing::Return(0));
  }

  ASSERT_EQ(controller_.Start(), prism::ControllerStatus::kSuccess);
  EXPECT_CALL(mock_strip_, Show()).Times(1);
  EXPECT_EQ(controller_.Run(), prism::ControllerStatus::kSuccess);
  EXPECT_EQ(g_last_scheduled_delay, 0U);
}

// ====================================================================
// Gradient instruction tests
// ====================================================================

TEST_F(ControllerTest, GradientZeroDurationAppliesExactTargetImmediately) {
  constexpr std::uint8_t index = 2U;
  constexpr prism::color::RgbColor source{10U, 20U, 30U};
  constexpr prism::color::RgbColor target{241U, 127U, 63U};
  auto* led = mock_strip_.MutableLed(index);

  AddGradient(controller_, index, target, 0U);
  ASSERT_EQ(controller_.Start(), prism::ControllerStatus::kSuccess);
  EXPECT_CALL(*led, Color()).WillOnce(testing::Return(source));
  EXPECT_CALL(*led, SetColor(target)).WillOnce(testing::Return(0));
  EXPECT_CALL(mock_strip_, Show()).Times(1);

  EXPECT_EQ(controller_.Run(), prism::ControllerStatus::kSuccess);
  EXPECT_EQ(g_last_scheduled_delay, 0U);
}

TEST_F(ControllerTest, GradientCompletesOnExactGridBoundary) {
  constexpr std::uint8_t index = 1U;
  constexpr prism::color::RgbColor source{180U, 20U, 60U};
  constexpr prism::color::RgbColor target{30U, 190U, 220U};
  constexpr std::uint16_t duration_ms = 40U;
  auto* led = mock_strip_.MutableLed(index);

  AddGradient(controller_, index, target, duration_ms);
  g_fake_time = 7U;
  ASSERT_EQ(controller_.Start(), prism::ControllerStatus::kSuccess);
  EXPECT_CALL(*led, Color()).WillOnce(testing::Return(source));
  EXPECT_CALL(mock_strip_, Show()).Times(0);
  EXPECT_EQ(controller_.Run(), prism::ControllerStatus::kSuccess);
  EXPECT_EQ(g_last_scheduled_delay, 20U);

  g_fake_time = 27U;
  g_last_scheduled_delay = 0U;
  const auto midpoint =
    prism::controller_detail::InterpolateRgb(source, target, 20U, duration_ms);
  EXPECT_CALL(*led, SetColor(midpoint)).WillOnce(testing::Return(0));
  EXPECT_CALL(mock_strip_, Show()).Times(1);
  EXPECT_EQ(controller_.Run(), prism::ControllerStatus::kSuccess);
  EXPECT_EQ(g_last_scheduled_delay, 20U);

  g_fake_time = 47U;
  g_last_scheduled_delay = 0U;
  EXPECT_CALL(*led, SetColor(target)).WillOnce(testing::Return(0));
  EXPECT_CALL(mock_strip_, Show()).Times(1);
  EXPECT_EQ(controller_.Run(), prism::ControllerStatus::kSuccess);
  EXPECT_EQ(g_last_scheduled_delay, 0U);
}

TEST_F(ControllerTest, GradientLateRunSkipsMissedSamplesAndCompletesOffGrid) {
  constexpr std::uint8_t index = 4U;
  constexpr prism::color::RgbColor source{200U, 30U, 15U};
  constexpr prism::color::RgbColor target{15U, 90U, 240U};
  constexpr std::uint16_t duration_ms = 45U;
  auto* led = mock_strip_.MutableLed(index);

  AddGradient(controller_, index, target, duration_ms);
  ASSERT_EQ(controller_.Start(), prism::ControllerStatus::kSuccess);
  EXPECT_CALL(*led, Color()).WillOnce(testing::Return(source));
  EXPECT_CALL(mock_strip_, Show()).Times(0);
  EXPECT_EQ(controller_.Run(), prism::ControllerStatus::kSuccess);
  EXPECT_EQ(g_last_scheduled_delay, 20U);

  g_fake_time = 33U;
  g_last_scheduled_delay = 0U;
  const auto late_sample =
    prism::controller_detail::InterpolateRgb(source, target, 33U, duration_ms);
  EXPECT_CALL(*led, SetColor(late_sample)).WillOnce(testing::Return(0));
  EXPECT_CALL(mock_strip_, Show()).Times(1);
  EXPECT_EQ(controller_.Run(), prism::ControllerStatus::kSuccess);
  EXPECT_EQ(g_last_scheduled_delay, 7U);

  g_fake_time = 40U;
  g_last_scheduled_delay = 0U;
  const auto grid_sample =
    prism::controller_detail::InterpolateRgb(source, target, 40U, duration_ms);
  EXPECT_CALL(*led, SetColor(grid_sample)).WillOnce(testing::Return(0));
  EXPECT_CALL(mock_strip_, Show()).Times(1);
  EXPECT_EQ(controller_.Run(), prism::ControllerStatus::kSuccess);
  EXPECT_EQ(g_last_scheduled_delay, 5U);

  g_fake_time = 45U;
  g_last_scheduled_delay = 0U;
  EXPECT_CALL(*led, SetColor(target)).WillOnce(testing::Return(0));
  EXPECT_CALL(mock_strip_, Show()).Times(1);
  EXPECT_EQ(controller_.Run(), prism::ControllerStatus::kSuccess);
  EXPECT_EQ(g_last_scheduled_delay, 0U);
}

TEST_F(ControllerTest, GradientElapsedTimeIsWrapSafe) {
  constexpr std::uint8_t index = 0U;
  constexpr std::uint32_t start_time =
    std::numeric_limits<std::uint32_t>::max() - 9U;
  constexpr prism::color::RgbColor source{150U, 70U, 10U};
  constexpr prism::color::RgbColor target{10U, 80U, 170U};
  constexpr std::uint16_t duration_ms = 25U;
  auto* led = mock_strip_.MutableLed(index);

  AddGradient(controller_, index, target, duration_ms);
  g_fake_time = start_time;
  ASSERT_EQ(controller_.Start(), prism::ControllerStatus::kSuccess);
  EXPECT_CALL(*led, Color()).WillOnce(testing::Return(source));
  EXPECT_CALL(mock_strip_, Show()).Times(0);
  EXPECT_EQ(controller_.Run(), prism::ControllerStatus::kSuccess);
  EXPECT_EQ(g_last_scheduled_delay, 20U);

  g_fake_time = 10U;
  g_last_scheduled_delay = 0U;
  const auto sample =
    prism::controller_detail::InterpolateRgb(source, target, 20U, duration_ms);
  EXPECT_CALL(*led, SetColor(sample)).WillOnce(testing::Return(0));
  EXPECT_CALL(mock_strip_, Show()).Times(1);
  EXPECT_EQ(controller_.Run(), prism::ControllerStatus::kSuccess);
  EXPECT_EQ(g_last_scheduled_delay, 5U);

  g_fake_time = 15U;
  g_last_scheduled_delay = 0U;
  EXPECT_CALL(*led, SetColor(target)).WillOnce(testing::Return(0));
  EXPECT_CALL(mock_strip_, Show()).Times(1);
  EXPECT_EQ(controller_.Run(), prism::ControllerStatus::kSuccess);
  EXPECT_EQ(g_last_scheduled_delay, 0U);
}

TEST_F(ControllerTest, GradientInvalidIndexCompletesWithoutWrites) {
  AddGradient(controller_, 9U, prism::color::RgbColor{10U, 20U, 30U}, 40U);
  ASSERT_EQ(controller_.Start(), prism::ControllerStatus::kSuccess);
  EXPECT_CALL(mock_strip_, Show()).Times(0);

  EXPECT_EQ(controller_.Run(), prism::ControllerStatus::kSuccess);
  EXPECT_EQ(g_last_scheduled_delay, 0U);
}

TEST_F(ControllerTest, GradientCapturesSameMarkColorAfterSetColor) {
  constexpr std::uint8_t index = 3U;
  constexpr prism::Mark mark = 20U;
  constexpr prism::color::RgbColor source{45U, 100U, 180U};
  constexpr prism::color::RgbColor target{220U, 75U, 25U};
  auto* led = mock_strip_.MutableLed(index);

  prism::SetSingleColor set_color;
  set_color.mark = mark;
  set_color.color = source;
  set_color.index = index;
  ASSERT_EQ(controller_.AddInstruction(&set_color),
            prism::ControllerStatus::kSuccess);
  AddGradient(controller_, index, target, 40U, mark);
  ASSERT_EQ(controller_.Start(), prism::ControllerStatus::kSuccess);

  EXPECT_CALL(*led, Color()).Times(0);
  EXPECT_CALL(*led, SetColor(testing::_)).Times(0);
  EXPECT_CALL(mock_strip_, Show()).Times(0);
  EXPECT_EQ(controller_.Run(), prism::ControllerStatus::kSuccess);
  EXPECT_EQ(g_last_scheduled_delay, mark);

  g_fake_time = mark;
  g_last_scheduled_delay = 0U;
  testing::InSequence same_mark_order;
  EXPECT_CALL(*led, SetColor(source)).WillOnce(testing::Return(0));
  EXPECT_CALL(*led, Color()).WillOnce(testing::Return(source));
  EXPECT_CALL(mock_strip_, Show()).Times(1);
  EXPECT_EQ(controller_.Run(), prism::ControllerStatus::kSuccess);
  EXPECT_EQ(g_last_scheduled_delay, 20U);

  g_fake_time = 40U;
  g_last_scheduled_delay = 0U;
  const auto sample =
    prism::controller_detail::InterpolateRgb(source, target, 20U, 40U);
  EXPECT_CALL(*led, SetColor(sample)).WillOnce(testing::Return(0));
  EXPECT_CALL(mock_strip_, Show()).Times(1);
  EXPECT_EQ(controller_.Run(), prism::ControllerStatus::kSuccess);
  EXPECT_EQ(g_last_scheduled_delay, 20U);

  g_fake_time = 60U;
  g_last_scheduled_delay = 0U;
  EXPECT_CALL(*led, SetColor(target)).WillOnce(testing::Return(0));
  EXPECT_CALL(mock_strip_, Show()).Times(1);
  EXPECT_EQ(controller_.Run(), prism::ControllerStatus::kSuccess);
  EXPECT_EQ(g_last_scheduled_delay, 0U);
}

TEST_F(ControllerTest, GradientGroupReplayCapturesThenCurrentStagedColor) {
  constexpr std::uint8_t index = 5U;
  constexpr prism::color::RgbColor initial{10U, 40U, 90U};
  constexpr prism::color::RgbColor replay_source{190U, 110U, 20U};
  constexpr prism::color::RgbColor target{20U, 160U, 235U};
  auto* led = mock_strip_.MutableLed(index);

  AddGradient(controller_, index, target, 40U);
  prism::SetSingleColor seed_replay;
  seed_replay.mark = 40U;
  seed_replay.color = replay_source;
  seed_replay.index = index;
  ASSERT_EQ(controller_.AddInstruction(&seed_replay),
            prism::ControllerStatus::kSuccess);
  ASSERT_EQ(controller_.Start(1U), prism::ControllerStatus::kSuccess);

  EXPECT_CALL(*led, Color()).WillOnce(testing::Return(initial));
  EXPECT_CALL(mock_strip_, Show()).Times(0);
  EXPECT_EQ(controller_.Run(), prism::ControllerStatus::kSuccess);
  EXPECT_EQ(g_last_scheduled_delay, 20U);

  g_fake_time = 20U;
  g_last_scheduled_delay = 0U;
  const auto first_pass_sample =
    prism::controller_detail::InterpolateRgb(initial, target, 20U, 40U);
  EXPECT_CALL(*led, SetColor(first_pass_sample)).WillOnce(testing::Return(0));
  EXPECT_CALL(mock_strip_, Show()).Times(1);
  EXPECT_EQ(controller_.Run(), prism::ControllerStatus::kSuccess);
  EXPECT_EQ(g_last_scheduled_delay, 20U);

  g_fake_time = 40U;
  g_last_scheduled_delay = 0U;
  {
    testing::InSequence replay_order;
    EXPECT_CALL(*led, SetColor(target)).WillOnce(testing::Return(0));
    EXPECT_CALL(*led, SetColor(replay_source)).WillOnce(testing::Return(0));
    EXPECT_CALL(*led, Color()).WillOnce(testing::Return(replay_source));
    EXPECT_CALL(mock_strip_, Show()).Times(1);
  }
  EXPECT_EQ(controller_.Run(), prism::ControllerStatus::kSuccess);
  EXPECT_EQ(g_last_scheduled_delay, 20U);

  g_fake_time = 60U;
  g_last_scheduled_delay = 0U;
  const auto replay_sample =
    prism::controller_detail::InterpolateRgb(replay_source, target, 20U, 40U);
  EXPECT_CALL(*led, SetColor(replay_sample)).WillOnce(testing::Return(0));
  EXPECT_CALL(mock_strip_, Show()).Times(1);
  EXPECT_EQ(controller_.Run(), prism::ControllerStatus::kSuccess);
  EXPECT_EQ(g_last_scheduled_delay, 20U);

  g_fake_time = 80U;
  g_last_scheduled_delay = 0U;
  {
    testing::InSequence replay_completion_order;
    EXPECT_CALL(*led, SetColor(target)).WillOnce(testing::Return(0));
    EXPECT_CALL(*led, SetColor(replay_source)).WillOnce(testing::Return(0));
    EXPECT_CALL(mock_strip_, Show()).Times(1);
  }
  EXPECT_EQ(controller_.Run(), prism::ControllerStatus::kSuccess);
  EXPECT_EQ(g_last_scheduled_delay, 0U);
}

TEST_F(ControllerTest, GradientResetLeavesLastStagedSampleInPlace) {
  constexpr std::uint8_t index = 6U;
  constexpr prism::color::RgbColor source{100U, 30U, 40U};
  constexpr prism::color::RgbColor target{15U, 220U, 180U};
  constexpr std::uint16_t duration_ms = 60U;
  prism::color::RgbColor staged = source;
  auto* led = mock_strip_.MutableLed(index);

  AddGradient(controller_, index, target, duration_ms);
  ASSERT_EQ(controller_.Start(), prism::ControllerStatus::kSuccess);
  EXPECT_CALL(*led, Color()).WillOnce(testing::Return(source));
  EXPECT_CALL(mock_strip_, Show()).Times(0);
  EXPECT_EQ(controller_.Run(), prism::ControllerStatus::kSuccess);

  g_fake_time = 20U;
  g_last_scheduled_delay = 0U;
  const auto last_sample =
    prism::controller_detail::InterpolateRgb(source, target, 20U, duration_ms);
  EXPECT_CALL(*led, SetColor(last_sample))
    .WillOnce([&staged](const prism::color::RgbColor& color) {
      staged = color;
      return 0;
    });
  EXPECT_CALL(mock_strip_, Show()).Times(1);
  EXPECT_EQ(controller_.Run(), prism::ControllerStatus::kSuccess);

  controller_.ResetInstructions();
  g_fake_time = 100U;
  EXPECT_CALL(*led, SetColor(testing::_)).Times(0);
  EXPECT_CALL(mock_strip_, Show()).Times(0);
  EXPECT_EQ(controller_.Run(), prism::ControllerStatus::kSuccess);
  EXPECT_EQ(staged, last_sample);
}

TEST_F(ControllerTest, GradientsOnOnePixelKeepOrderAndCoalesceShow) {
  constexpr std::uint8_t index = 1U;
  constexpr prism::color::RgbColor source{100U, 70U, 40U};
  constexpr prism::color::RgbColor first_target{240U, 30U, 100U};
  constexpr prism::color::RgbColor second_target{20U, 180U, 250U};
  constexpr std::uint16_t duration_ms = 40U;
  auto* led = mock_strip_.MutableLed(index);

  AddGradient(controller_, index, first_target, duration_ms);
  AddGradient(controller_, index, second_target, duration_ms);
  ASSERT_EQ(controller_.Start(), prism::ControllerStatus::kSuccess);
  EXPECT_CALL(*led, Color())
    .WillOnce(testing::Return(source))
    .WillOnce(testing::Return(source));
  EXPECT_CALL(mock_strip_, Show()).Times(0);
  EXPECT_EQ(controller_.Run(), prism::ControllerStatus::kSuccess);
  EXPECT_EQ(g_last_scheduled_delay, 20U);

  g_fake_time = 20U;
  g_last_scheduled_delay = 0U;
  const auto first_sample = prism::controller_detail::InterpolateRgb(
    source, first_target, 20U, duration_ms);
  const auto second_sample = prism::controller_detail::InterpolateRgb(
    source, second_target, 20U, duration_ms);
  {
    testing::InSequence update_order;
    EXPECT_CALL(*led, SetColor(first_sample)).WillOnce(testing::Return(0));
    EXPECT_CALL(*led, SetColor(second_sample)).WillOnce(testing::Return(0));
    EXPECT_CALL(mock_strip_, Show()).Times(1);
  }
  EXPECT_EQ(controller_.Run(), prism::ControllerStatus::kSuccess);
  EXPECT_EQ(g_last_scheduled_delay, 20U);

  g_fake_time = 40U;
  g_last_scheduled_delay = 0U;
  {
    testing::InSequence endpoint_order;
    EXPECT_CALL(*led, SetColor(first_target)).WillOnce(testing::Return(0));
    EXPECT_CALL(*led, SetColor(second_target)).WillOnce(testing::Return(0));
    EXPECT_CALL(mock_strip_, Show()).Times(1);
  }
  EXPECT_EQ(controller_.Run(), prism::ControllerStatus::kSuccess);
  EXPECT_EQ(g_last_scheduled_delay, 0U);
}

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

  slot.Execute(0U);
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

  slot.Execute(0U);
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

  slot.Execute(0U);
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
  slot.Execute(0U);

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
  slot.Execute(0U);
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

  slot.Execute(0U);
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
  slot.Execute(0U);
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

  slot.Execute(0U);
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

  ASSERT_EQ(controller_.Start(), prism::ControllerStatus::kSuccess);
  // Controller calls show() once after all instructions.
  EXPECT_CALL(mock_strip_, Show()).Times(1);

  controller_.Run();
}

/// @brief Run() with zero instructions is a no-op.
TEST_F(ControllerTest, RunOnEmptyControllerIsNoop) {
  EXPECT_CALL(mock_strip_, Show()).Times(0);
  controller_.Run();
}

/// @brief Run() does not implicitly start a loaded program.
TEST_F(ControllerTest, RunOnLoadedEditableProgramIsNoop) {
  prism::SetSingleColor instruction;
  instruction.mark = 0U;
  ASSERT_EQ(controller_.AddInstruction(&instruction),
            prism::ControllerStatus::kSuccess);

  EXPECT_CALL(*mock_strip_.MutableLed(0U), SetColor(testing::_)).Times(0);
  EXPECT_CALL(mock_strip_, Show()).Times(0);
  EXPECT_EQ(controller_.Run(), prism::ControllerStatus::kSuccess);
  EXPECT_EQ(controller_.AddInstruction(&instruction),
            prism::ControllerStatus::kSuccess);
}

/// @brief Start initializes the root but leaves the initial tick to its caller.
TEST_F(ControllerTest, StartDefersMarkZeroWorkUntilRun) {
  prism::SetSingleColor instruction;
  instruction.mark = 0U;
  ASSERT_EQ(controller_.AddInstruction(&instruction),
            prism::ControllerStatus::kSuccess);

  EXPECT_CALL(*mock_strip_.MutableLed(0U), SetColor(testing::_)).Times(0);
  EXPECT_CALL(mock_strip_, Show()).Times(0);
  ASSERT_EQ(controller_.Start(), prism::ControllerStatus::kSuccess);
  testing::Mock::VerifyAndClearExpectations(&mock_strip_);

  EXPECT_CALL(*mock_strip_.MutableLed(0U), SetColor(testing::_)).Times(1);
  EXPECT_CALL(mock_strip_, Show()).Times(1);
  EXPECT_EQ(controller_.Run(), prism::ControllerStatus::kSuccess);
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

  slot.Execute(0U);
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
  slot.Execute(0U);

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
  slot.Execute(0U);
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
  ASSERT_EQ(controller_.Start(), prism::ControllerStatus::kSuccess);

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
  ASSERT_EQ(controller_.Start(), prism::ControllerStatus::kSuccess);

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
  ASSERT_EQ(controller_.Start(), prism::ControllerStatus::kSuccess);

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
  ASSERT_EQ(controller_.Start(), prism::ControllerStatus::kSuccess);

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
  ASSERT_EQ(controller_.Start(), prism::ControllerStatus::kSuccess);

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
  EXPECT_EQ(slot.Execute(0U), 0U);
}

/// @brief The host-only timed variant yields and resets through fixed storage.
TEST_F(ControllerTest, TimedTestInstructionSlotYieldsAndResets) {
  std::uint32_t execution_count = 0U;
  prism::test::TimedInstruction instruction{25U, &execution_count};
  prism::InstructionMemorySlot slot;

  ASSERT_TRUE(slot.Set(&instruction));
  EXPECT_TRUE(slot.HasPositiveSchedulingBoundary());
  EXPECT_EQ(slot.Execute(0U), 25U);
  EXPECT_EQ(execution_count, 1U);

  slot.ResetForReplay();
  EXPECT_EQ(slot.Execute(0U), 25U);
  EXPECT_EQ(slot.Execute(0U), 0U);
  EXPECT_EQ(execution_count, 3U);
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
  runtime.positive_boundary_observed = true;
  runtime.parent_group_index = 3U;
  runtime.depth = 3U;
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
  EXPECT_FALSE(runtime.positive_boundary_observed);
  EXPECT_EQ(runtime.parent_group_index, prism::kMaxGroups);
  EXPECT_EQ(runtime.depth, 0U);
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

/// @brief Twenty-eight sparse non-root labels fit beside the root record.
TEST_F(ControllerTest, MaximumGroupCountIncludesImplicitRoot) {
  for (std::uint32_t i = 0U; i < prism::Controller::kMaxInstruction; ++i) {
    prism::SetSingleColor instruction;
    instruction.group_id = 0x80000000U + i * 257U;
    ASSERT_EQ(controller_.AddInstruction(&instruction),
              prism::ControllerStatus::kSuccess);
  }

  EXPECT_EQ(prism::Controller::kMaxInstruction, 28U);
  EXPECT_EQ(prism::Controller::kMaxGroups, 29U);
  EXPECT_EQ(controller_.Start(), prism::ControllerStatus::kSuccess);
  prism::SetSingleColor replacement;
  EXPECT_EQ(controller_.AddInstruction(&replacement),
            prism::ControllerStatus::kBusy);
}

/// @brief Seven staggered forever children occupy the 28-slot program safely.
TEST_F(ControllerTest, SevenStaggeredForeverChildrenRepeatConcurrently) {
  constexpr std::array<prism::Mark, 7U> kMarks{0U,    286U,  571U, 857U,
                                               1143U, 1429U, 1714U};
  std::array<std::array<std::uint32_t, 2U>, 7U> child_calls{};

  for (std::size_t i = 0U; i < kMarks.size(); ++i) {
    EXPECT_CALL(*mock_strip_.MutableLed(i), SetColor(testing::_)).Times(1);

    prism::SetSingleColor color;
    color.mark = kMarks[i];
    color.index = static_cast<std::uint8_t>(i);
    color.color = {static_cast<std::uint8_t>(20U + i),
                   static_cast<std::uint8_t>(40U + i),
                   static_cast<std::uint8_t>(60U + i)};
    ASSERT_EQ(controller_.AddInstruction(&color),
              prism::ControllerStatus::kSuccess);

    prism::RunGroupPayload payload{};
    payload.group_id = 0U;
    payload.mark = kMarks[i];
    payload.target_id = static_cast<prism::GroupId>(i + 1U);
    payload.additional_repeats = prism::kForeverLoopCount;
    prism::RunGroupInstruction start_child{payload};
    ASSERT_EQ(controller_.AddInstruction(&start_child),
              prism::ControllerStatus::kSuccess);
  }
  EXPECT_CALL(mock_strip_, Show()).Times(7);

  for (std::size_t i = 0U; i < kMarks.size(); ++i) {
    prism::test::TimedInstruction first{10U, &child_calls[i][0U]};
    first.group_id = static_cast<prism::GroupId>(i + 1U);
    prism::test::TimedInstruction second{10U, &child_calls[i][1U]};
    second.group_id = first.group_id;
    ASSERT_EQ(controller_.AddInstruction(&first),
              prism::ControllerStatus::kSuccess);
    ASSERT_EQ(controller_.AddInstruction(&second),
              prism::ControllerStatus::kSuccess);
  }

  EXPECT_EQ(prism::Controller::kMaxInstruction, 28U);
  prism::SetSingleColor overflow;
  EXPECT_EQ(controller_.AddInstruction(&overflow),
            prism::ControllerStatus::kCapacityExhausted);

  ASSERT_EQ(controller_.Start(), prism::ControllerStatus::kSuccess);
  EXPECT_EQ(controller_.Run(), prism::ControllerStatus::kSuccess);
  for (std::size_t i = 1U; i < kMarks.size(); ++i) {
    g_fake_time = kMarks[i];
    EXPECT_EQ(controller_.Run(), prism::ControllerStatus::kSuccess);
  }

  // Each child completes and starts another pass while all seven remain
  // outstanding; the root therefore remains active after its final mark.
  g_fake_time = static_cast<std::uint32_t>(kMarks.back()) + 10U;
  EXPECT_EQ(controller_.Run(), prism::ControllerStatus::kSuccess);
  for (const auto& calls : child_calls) {
    EXPECT_GE(calls[0U], 3U);
    EXPECT_GE(calls[1U], 3U);
  }
  EXPECT_EQ(controller_.Start(), prism::ControllerStatus::kBusy);
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

/// @brief A child forever repeat still requires a positive time boundary.
TEST_F(ControllerTest, ChildForeverRepeatWithoutBoundaryFailsPreflight) {
  prism::RunGroupPayload payload{};
  payload.group_id = 0U;
  payload.target_id = 0xFFFF0001U;
  payload.additional_repeats = prism::kForeverLoopCount;
  prism::RunGroupInstruction child_call{payload};
  prism::SetSingleColor child;
  child.group_id = payload.target_id;

  ASSERT_EQ(controller_.AddInstruction(&child_call),
            prism::ControllerStatus::kSuccess);
  ASSERT_EQ(controller_.AddInstruction(&child),
            prism::ControllerStatus::kSuccess);
  EXPECT_EQ(controller_.Start(), prism::ControllerStatus::kInvalidProgram);
}

/// @brief Child marks use their activation origin while parents keep running.
TEST_F(ControllerTest, ParentProgressesWhileChildUsesLocalMarks) {
  prism::RunGroupPayload child_call_payload{};
  child_call_payload.group_id = 0U;
  child_call_payload.mark = 20U;
  child_call_payload.target_id = 0x80000001U;
  prism::RunGroupInstruction child_call{child_call_payload};
  prism::SetSingleColor parent_instruction;
  parent_instruction.mark = 25U;
  parent_instruction.index = 0U;
  prism::SetSingleColor child_instruction;
  child_instruction.group_id = child_call_payload.target_id;
  child_instruction.mark = 10U;
  child_instruction.index = 1U;

  ASSERT_EQ(controller_.AddInstruction(&child_call),
            prism::ControllerStatus::kSuccess);
  ASSERT_EQ(controller_.AddInstruction(&parent_instruction),
            prism::ControllerStatus::kSuccess);
  ASSERT_EQ(controller_.AddInstruction(&child_instruction),
            prism::ControllerStatus::kSuccess);
  ASSERT_EQ(controller_.Start(), prism::ControllerStatus::kSuccess);

  EXPECT_CALL(mock_strip_, Show()).Times(0);
  EXPECT_EQ(controller_.Run(), prism::ControllerStatus::kSuccess);
  EXPECT_EQ(g_last_scheduled_delay, 20U);

  g_fake_time = 20U;
  g_last_scheduled_delay = 0U;
  EXPECT_CALL(mock_strip_, Show()).Times(0);
  EXPECT_EQ(controller_.Run(), prism::ControllerStatus::kSuccess);
  EXPECT_EQ(g_last_scheduled_delay, 5U);

  g_fake_time = 25U;
  g_last_scheduled_delay = 0U;
  EXPECT_CALL(*mock_strip_.MutableLed(0U), SetColor(testing::_)).Times(1);
  EXPECT_CALL(*mock_strip_.MutableLed(1U), SetColor(testing::_)).Times(0);
  EXPECT_CALL(mock_strip_, Show()).Times(1);
  EXPECT_EQ(controller_.Run(), prism::ControllerStatus::kSuccess);
  EXPECT_EQ(g_last_scheduled_delay, 5U);
  testing::Mock::VerifyAndClearExpectations(&mock_strip_);

  g_fake_time = 30U;
  g_last_scheduled_delay = 0U;
  EXPECT_CALL(*mock_strip_.MutableLed(0U), SetColor(testing::_)).Times(0);
  EXPECT_CALL(*mock_strip_.MutableLed(1U), SetColor(testing::_)).Times(1);
  EXPECT_CALL(mock_strip_, Show()).Times(1);
  EXPECT_EQ(controller_.Run(), prism::ControllerStatus::kSuccess);
  EXPECT_EQ(g_last_scheduled_delay, 0U);
}

/// @brief The earliest child deadline, parent mark, and root deadline wins.
TEST_F(ControllerTest, SchedulesEarliestDeadlineAcrossActiveGroups) {
  std::uint32_t root_calls = 0U;
  std::uint32_t child_calls = 0U;
  prism::test::TimedInstruction root_timed{100U, &root_calls};
  prism::RunGroupPayload child_call_payload{};
  child_call_payload.group_id = 0U;
  child_call_payload.target_id = 0x80000002U;
  prism::RunGroupInstruction child_call{child_call_payload};
  prism::SetSingleColor parent_instruction;
  parent_instruction.mark = 50U;
  prism::test::TimedInstruction child_timed{40U, &child_calls};
  child_timed.group_id = child_call_payload.target_id;

  ASSERT_EQ(controller_.AddInstruction(&root_timed),
            prism::ControllerStatus::kSuccess);
  ASSERT_EQ(controller_.AddInstruction(&child_call),
            prism::ControllerStatus::kSuccess);
  ASSERT_EQ(controller_.AddInstruction(&parent_instruction),
            prism::ControllerStatus::kSuccess);
  ASSERT_EQ(controller_.AddInstruction(&child_timed),
            prism::ControllerStatus::kSuccess);
  ASSERT_EQ(controller_.Start(), prism::ControllerStatus::kSuccess);

  EXPECT_CALL(mock_strip_, Show()).Times(0);
  EXPECT_EQ(controller_.Run(), prism::ControllerStatus::kSuccess);
  EXPECT_EQ(root_calls, 1U);
  EXPECT_EQ(child_calls, 1U);
  EXPECT_EQ(g_last_scheduled_delay, 40U);

  g_fake_time = 40U;
  g_last_scheduled_delay = 0U;
  EXPECT_CALL(mock_strip_, Show()).Times(0);
  EXPECT_EQ(controller_.Run(), prism::ControllerStatus::kSuccess);
  EXPECT_EQ(root_calls, 1U);
  EXPECT_EQ(child_calls, 2U);
  EXPECT_EQ(g_last_scheduled_delay, 10U);

  g_fake_time = 50U;
  g_last_scheduled_delay = 0U;
  EXPECT_CALL(*mock_strip_.MutableLed(0U), SetColor(testing::_)).Times(1);
  EXPECT_CALL(mock_strip_, Show()).Times(1);
  EXPECT_EQ(controller_.Run(), prism::ControllerStatus::kSuccess);
  EXPECT_EQ(root_calls, 1U);
  EXPECT_EQ(g_last_scheduled_delay, 50U);

  g_fake_time = 100U;
  g_last_scheduled_delay = 0U;
  EXPECT_CALL(mock_strip_, Show()).Times(0);
  EXPECT_EQ(controller_.Run(), prism::ControllerStatus::kSuccess);
  EXPECT_EQ(root_calls, 2U);
  EXPECT_EQ(g_last_scheduled_delay, 0U);
}

/// @brief A finite child repeat resets its timed instruction for each pass.
TEST_F(ControllerTest, FiniteChildRepeatReplaysTimedInstruction) {
  std::uint32_t child_calls = 0U;
  prism::RunGroupPayload child_call_payload{};
  child_call_payload.group_id = 0U;
  child_call_payload.target_id = 0x80000003U;
  child_call_payload.additional_repeats = 1U;
  prism::RunGroupInstruction child_call{child_call_payload};
  prism::test::TimedInstruction child_timed{10U, &child_calls};
  child_timed.group_id = child_call_payload.target_id;

  ASSERT_EQ(controller_.AddInstruction(&child_call),
            prism::ControllerStatus::kSuccess);
  ASSERT_EQ(controller_.AddInstruction(&child_timed),
            prism::ControllerStatus::kSuccess);
  ASSERT_EQ(controller_.Start(), prism::ControllerStatus::kSuccess);

  EXPECT_EQ(controller_.Run(), prism::ControllerStatus::kSuccess);
  EXPECT_EQ(child_calls, 1U);
  EXPECT_EQ(g_last_scheduled_delay, 10U);

  g_fake_time = 10U;
  g_last_scheduled_delay = 0U;
  EXPECT_EQ(controller_.Run(), prism::ControllerStatus::kSuccess);
  EXPECT_EQ(child_calls, 3U);
  EXPECT_EQ(g_last_scheduled_delay, 10U);

  g_fake_time = 20U;
  g_last_scheduled_delay = 0U;
  EXPECT_EQ(controller_.Run(), prism::ControllerStatus::kSuccess);
  EXPECT_EQ(child_calls, 4U);
  EXPECT_EQ(g_last_scheduled_delay, 0U);
}

/// @brief Root repeats replay timed state and start each pass at completion.
TEST_F(ControllerTest, FiniteRootRepeatReplaysTimedInstruction) {
  std::uint32_t execution_count = 0U;
  prism::test::TimedInstruction timed{10U, &execution_count};
  ASSERT_EQ(controller_.AddInstruction(&timed),
            prism::ControllerStatus::kSuccess);
  ASSERT_EQ(controller_.Start(1U), prism::ControllerStatus::kSuccess);

  EXPECT_EQ(controller_.Run(), prism::ControllerStatus::kSuccess);
  EXPECT_EQ(execution_count, 1U);
  EXPECT_EQ(g_last_scheduled_delay, 10U);

  g_fake_time = 10U;
  g_last_scheduled_delay = 0U;
  EXPECT_EQ(controller_.Run(), prism::ControllerStatus::kSuccess);
  EXPECT_EQ(execution_count, 3U);
  EXPECT_EQ(g_last_scheduled_delay, 10U);
  EXPECT_EQ(controller_.Start(), prism::ControllerStatus::kBusy);

  g_fake_time = 20U;
  g_last_scheduled_delay = 0U;
  EXPECT_EQ(controller_.Run(), prism::ControllerStatus::kSuccess);
  EXPECT_EQ(execution_count, 4U);
  EXPECT_EQ(g_last_scheduled_delay, 0U);
}

/// @brief The forever sentinel repeats only across a positive time boundary.
TEST_F(ControllerTest, RootForeverRepeatsAcrossTimedBoundary) {
  std::uint32_t execution_count = 0U;
  prism::test::TimedInstruction timed{5U, &execution_count};
  ASSERT_EQ(controller_.AddInstruction(&timed),
            prism::ControllerStatus::kSuccess);
  ASSERT_EQ(controller_.Start(prism::kForeverLoopCount),
            prism::ControllerStatus::kSuccess);

  EXPECT_EQ(controller_.Run(), prism::ControllerStatus::kSuccess);
  EXPECT_EQ(execution_count, 1U);

  g_fake_time = 5U;
  g_last_scheduled_delay = 0U;
  EXPECT_EQ(controller_.Run(), prism::ControllerStatus::kSuccess);
  EXPECT_EQ(execution_count, 3U);
  EXPECT_EQ(g_last_scheduled_delay, 5U);

  g_fake_time = 10U;
  g_last_scheduled_delay = 0U;
  EXPECT_EQ(controller_.Run(), prism::ControllerStatus::kSuccess);
  EXPECT_EQ(execution_count, 5U);
  EXPECT_EQ(g_last_scheduled_delay, 5U);
  EXPECT_EQ(controller_.Start(), prism::ControllerStatus::kBusy);
}

/// @brief A child-only boundary delays a repeated root until descendants end.
TEST_F(ControllerTest,
       NestedChildBoundaryDelaysRepeatedRootUntilChainCompletes) {
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
  leaf.index = 2U;

  ASSERT_EQ(controller_.AddInstruction(&root_call),
            prism::ControllerStatus::kSuccess);
  ASSERT_EQ(controller_.AddInstruction(&nested_call),
            prism::ControllerStatus::kSuccess);
  ASSERT_EQ(controller_.AddInstruction(&leaf),
            prism::ControllerStatus::kSuccess);
  ASSERT_EQ(controller_.Start(1U), prism::ControllerStatus::kSuccess);

  EXPECT_CALL(*mock_strip_.MutableLed(2U), SetColor(testing::_)).Times(0);
  EXPECT_CALL(mock_strip_, Show()).Times(0);
  EXPECT_EQ(controller_.Run(), prism::ControllerStatus::kSuccess);
  EXPECT_EQ(g_last_scheduled_delay, 5U);
  testing::Mock::VerifyAndClearExpectations(&mock_strip_);

  g_fake_time = 5U;
  g_last_scheduled_delay = 0U;
  EXPECT_CALL(*mock_strip_.MutableLed(2U), SetColor(testing::_)).Times(1);
  EXPECT_CALL(mock_strip_, Show()).Times(1);
  EXPECT_EQ(controller_.Run(), prism::ControllerStatus::kSuccess);
  EXPECT_EQ(g_last_scheduled_delay, 5U);
  EXPECT_EQ(controller_.Start(), prism::ControllerStatus::kBusy);
  testing::Mock::VerifyAndClearExpectations(&mock_strip_);

  g_fake_time = 9U;
  g_last_scheduled_delay = 0U;
  EXPECT_CALL(*mock_strip_.MutableLed(2U), SetColor(testing::_)).Times(0);
  EXPECT_CALL(mock_strip_, Show()).Times(0);
  EXPECT_EQ(controller_.Run(), prism::ControllerStatus::kSuccess);
  EXPECT_EQ(g_last_scheduled_delay, 1U);
  testing::Mock::VerifyAndClearExpectations(&mock_strip_);

  g_fake_time = 10U;
  g_last_scheduled_delay = 0U;
  EXPECT_CALL(*mock_strip_.MutableLed(2U), SetColor(testing::_)).Times(1);
  EXPECT_CALL(mock_strip_, Show()).Times(1);
  EXPECT_EQ(controller_.Run(), prism::ControllerStatus::kSuccess);
  EXPECT_EQ(g_last_scheduled_delay, 0U);
}

/// @brief A duplicate active child fails without disturbing the first call.
TEST_F(ControllerTest, ActiveGroupConflictLeavesFirstInvocationRunning) {
  constexpr prism::GroupId child_group = 0x81000001U;
  prism::RunGroupPayload first_payload{};
  first_payload.group_id = 0U;
  first_payload.target_id = child_group;
  prism::RunGroupInstruction first_call{first_payload};
  prism::RunGroupInstruction duplicate_call{first_payload};
  prism::SetSingleColor child;
  child.group_id = child_group;
  child.mark = 10U;
  child.index = 3U;

  ASSERT_EQ(controller_.AddInstruction(&first_call),
            prism::ControllerStatus::kSuccess);
  ASSERT_EQ(controller_.AddInstruction(&duplicate_call),
            prism::ControllerStatus::kSuccess);
  ASSERT_EQ(controller_.AddInstruction(&child),
            prism::ControllerStatus::kSuccess);
  ASSERT_EQ(controller_.Start(), prism::ControllerStatus::kSuccess);

  EXPECT_CALL(*mock_strip_.MutableLed(3U), SetColor(testing::_)).Times(0);
  EXPECT_CALL(mock_strip_, Show()).Times(0);
  EXPECT_EQ(controller_.Run(), prism::ControllerStatus::kAlreadyActiveGroup);
  EXPECT_EQ(g_last_scheduled_delay, 10U);
  testing::Mock::VerifyAndClearExpectations(&mock_strip_);

  g_fake_time = 10U;
  g_last_scheduled_delay = 0U;
  EXPECT_CALL(*mock_strip_.MutableLed(3U), SetColor(testing::_)).Times(1);
  EXPECT_CALL(mock_strip_, Show()).Times(1);
  EXPECT_EQ(controller_.Run(), prism::ControllerStatus::kSuccess);
  EXPECT_EQ(g_last_scheduled_delay, 0U);
}

/// @brief Goal: stop a repeat whose only child boundary was rejected.
/// @details Steps: Start B, then max-repeat A whose mark-0 call targets active
///     B; verify A runs once, reports the conflict, and B keeps the root busy
///     until its own future mark completes.
TEST_F(ControllerTest, RepeatedGroupStopsWhenOnlyChildBoundaryConflicts) {
  constexpr prism::GroupId group_a = 0x82000001U;
  constexpr prism::GroupId group_b = 0x82000002U;
  prism::RunGroupPayload start_b_payload{};
  start_b_payload.group_id = 0U;
  start_b_payload.target_id = group_b;
  prism::RunGroupInstruction start_b{start_b_payload};
  prism::RunGroupPayload start_a_payload{};
  start_a_payload.group_id = 0U;
  start_a_payload.target_id = group_a;
  start_a_payload.additional_repeats = prism::kMaxFiniteLoopCount;
  prism::RunGroupInstruction start_a{start_a_payload};
  std::uint32_t a_passes = 0U;
  prism::test::TimedInstruction count_a_pass{0U, &a_passes, false};
  count_a_pass.group_id = group_a;
  prism::RunGroupPayload conflict_payload{};
  conflict_payload.group_id = group_a;
  conflict_payload.target_id = group_b;
  prism::RunGroupInstruction conflict_call{conflict_payload};
  prism::SetSingleColor delayed_b;
  delayed_b.group_id = group_b;
  delayed_b.mark = 20U;
  delayed_b.index = 4U;

  ASSERT_EQ(controller_.AddInstruction(&start_b),
            prism::ControllerStatus::kSuccess);
  ASSERT_EQ(controller_.AddInstruction(&start_a),
            prism::ControllerStatus::kSuccess);
  ASSERT_EQ(controller_.AddInstruction(&count_a_pass),
            prism::ControllerStatus::kSuccess);
  ASSERT_EQ(controller_.AddInstruction(&conflict_call),
            prism::ControllerStatus::kSuccess);
  ASSERT_EQ(controller_.AddInstruction(&delayed_b),
            prism::ControllerStatus::kSuccess);
  ASSERT_EQ(controller_.Start(), prism::ControllerStatus::kSuccess);

  EXPECT_CALL(*mock_strip_.MutableLed(4U), SetColor(testing::_)).Times(0);
  EXPECT_CALL(mock_strip_, Show()).Times(0);
  EXPECT_EQ(controller_.Run(), prism::ControllerStatus::kAlreadyActiveGroup);
  EXPECT_EQ(a_passes, 1U);
  EXPECT_EQ(g_last_scheduled_delay, 20U);
  EXPECT_EQ(controller_.Start(), prism::ControllerStatus::kBusy);
  testing::Mock::VerifyAndClearExpectations(&mock_strip_);

  g_fake_time = 20U;
  g_last_scheduled_delay = 0U;
  EXPECT_CALL(*mock_strip_.MutableLed(4U), SetColor(delayed_b.color)).Times(1);
  EXPECT_CALL(mock_strip_, Show()).Times(1);
  EXPECT_EQ(controller_.Run(), prism::ControllerStatus::kSuccess);
  EXPECT_EQ(g_last_scheduled_delay, 0U);
  EXPECT_EQ(controller_.Start(), prism::ControllerStatus::kSuccess);
}

/// @brief Goal: observe a failed child call's own positive mark when due.
/// @details Steps: Keep B active while repeated A waits for mark 10; after A's
///     call conflicts, verify it schedules its next pass from that boundary,
///     then releases the root while B continues to mark 30.
TEST_F(ControllerTest, FailedChildCallObservesItsPositiveMarkWhenDue) {
  constexpr prism::GroupId group_a = 0x82000003U;
  constexpr prism::GroupId group_b = 0x82000004U;
  prism::RunGroupPayload start_b_payload{};
  start_b_payload.group_id = 0U;
  start_b_payload.target_id = group_b;
  prism::RunGroupInstruction start_b{start_b_payload};
  prism::RunGroupPayload start_a_payload{};
  start_a_payload.group_id = 0U;
  start_a_payload.target_id = group_a;
  start_a_payload.additional_repeats = 1U;
  prism::RunGroupInstruction start_a{start_a_payload};
  prism::RunGroupPayload conflict_payload{};
  conflict_payload.group_id = group_a;
  conflict_payload.mark = 10U;
  conflict_payload.target_id = group_b;
  prism::RunGroupInstruction conflict_call{conflict_payload};
  prism::SetSingleColor delayed_b;
  delayed_b.group_id = group_b;
  delayed_b.mark = 30U;
  delayed_b.index = 5U;

  ASSERT_EQ(controller_.AddInstruction(&start_b),
            prism::ControllerStatus::kSuccess);
  ASSERT_EQ(controller_.AddInstruction(&start_a),
            prism::ControllerStatus::kSuccess);
  ASSERT_EQ(controller_.AddInstruction(&conflict_call),
            prism::ControllerStatus::kSuccess);
  ASSERT_EQ(controller_.AddInstruction(&delayed_b),
            prism::ControllerStatus::kSuccess);
  ASSERT_EQ(controller_.Start(), prism::ControllerStatus::kSuccess);

  EXPECT_CALL(*mock_strip_.MutableLed(5U), SetColor(testing::_)).Times(0);
  EXPECT_CALL(mock_strip_, Show()).Times(0);
  EXPECT_EQ(controller_.Run(), prism::ControllerStatus::kSuccess);
  EXPECT_EQ(g_last_scheduled_delay, 10U);
  testing::Mock::VerifyAndClearExpectations(&mock_strip_);

  g_fake_time = 10U;
  g_last_scheduled_delay = 0U;
  EXPECT_CALL(*mock_strip_.MutableLed(5U), SetColor(testing::_)).Times(0);
  EXPECT_CALL(mock_strip_, Show()).Times(0);
  EXPECT_EQ(controller_.Run(), prism::ControllerStatus::kAlreadyActiveGroup);
  EXPECT_EQ(g_last_scheduled_delay, 10U);
  EXPECT_EQ(controller_.Start(), prism::ControllerStatus::kBusy);
  testing::Mock::VerifyAndClearExpectations(&mock_strip_);

  g_fake_time = 20U;
  g_last_scheduled_delay = 0U;
  EXPECT_CALL(*mock_strip_.MutableLed(5U), SetColor(testing::_)).Times(0);
  EXPECT_CALL(mock_strip_, Show()).Times(0);
  EXPECT_EQ(controller_.Run(), prism::ControllerStatus::kAlreadyActiveGroup);
  EXPECT_EQ(g_last_scheduled_delay, 10U);
  testing::Mock::VerifyAndClearExpectations(&mock_strip_);

  g_fake_time = 30U;
  g_last_scheduled_delay = 0U;
  EXPECT_CALL(*mock_strip_.MutableLed(5U), SetColor(delayed_b.color)).Times(1);
  EXPECT_CALL(mock_strip_, Show()).Times(1);
  EXPECT_EQ(controller_.Run(), prism::ControllerStatus::kSuccess);
  EXPECT_EQ(g_last_scheduled_delay, 0U);
  EXPECT_EQ(controller_.Start(), prism::ControllerStatus::kSuccess);
}

/// @brief Goal: propagate a resumed descendant deadline to a repeated root.
/// @details Steps: Start a child timed instruction beneath a repeated root;
///     verify its deadline starts the next root pass, then completes the run
///     after the second child pass.
TEST_F(ControllerTest, ChildTimedBoundaryPropagatesToRepeatedRoot) {
  constexpr prism::GroupId child_group = 0x82000005U;
  std::uint32_t child_calls = 0U;
  prism::RunGroupPayload child_call_payload{};
  child_call_payload.group_id = 0U;
  child_call_payload.target_id = child_group;
  prism::RunGroupInstruction child_call{child_call_payload};
  prism::test::TimedInstruction child_timed{5U, &child_calls};
  child_timed.group_id = child_group;

  ASSERT_EQ(controller_.AddInstruction(&child_call),
            prism::ControllerStatus::kSuccess);
  ASSERT_EQ(controller_.AddInstruction(&child_timed),
            prism::ControllerStatus::kSuccess);
  ASSERT_EQ(controller_.Start(1U), prism::ControllerStatus::kSuccess);

  EXPECT_CALL(mock_strip_, Show()).Times(0);
  EXPECT_EQ(controller_.Run(), prism::ControllerStatus::kSuccess);
  EXPECT_EQ(child_calls, 1U);
  EXPECT_EQ(g_last_scheduled_delay, 5U);

  g_fake_time = 5U;
  g_last_scheduled_delay = 0U;
  EXPECT_EQ(controller_.Run(), prism::ControllerStatus::kSuccess);
  EXPECT_EQ(child_calls, 3U);
  EXPECT_EQ(g_last_scheduled_delay, 5U);

  g_fake_time = 10U;
  g_last_scheduled_delay = 0U;
  EXPECT_EQ(controller_.Run(), prism::ControllerStatus::kSuccess);
  EXPECT_EQ(child_calls, 4U);
  EXPECT_EQ(g_last_scheduled_delay, 0U);
}

/// @brief Goal: surface a repeated-boundary invariant failure without an error.
/// @details Steps: Start a repeated test instruction that advertises a positive
///     boundary but completes immediately; verify one execution,
///     kInvalidProgram, and a completed/replayable program.
TEST_F(ControllerTest, RepeatedBoundaryMismatchReturnsInvalidProgram) {
  std::uint32_t execution_count = 0U;
  prism::test::TimedInstruction no_yield{5U, &execution_count, false};
  ASSERT_EQ(controller_.AddInstruction(&no_yield),
            prism::ControllerStatus::kSuccess);
  ASSERT_EQ(controller_.Start(1U), prism::ControllerStatus::kSuccess);

  EXPECT_EQ(controller_.Run(), prism::ControllerStatus::kInvalidProgram);
  EXPECT_EQ(execution_count, 1U);
  EXPECT_EQ(controller_.Start(1U), prism::ControllerStatus::kSuccess);
}

/// @brief The worked timeline overlaps parent work and a child repeat.
TEST_F(ControllerTest, WorkedTimelineKeepsParentAndChildPassesInSync) {
  const auto red = prism::color::ToRgb(prism::color::Preset::kPureRed);
  const auto blue = prism::color::ToRgb(prism::color::Preset::kPureBlue);
  const prism::color::RgbColor yellow{255U, 255U, 0U};
  const auto green = prism::color::ToRgb(prism::color::Preset::kPureGreen);
  std::uint32_t delay_calls = 0U;

  prism::SetSingleColor root_red;
  root_red.mark = 100U;
  root_red.color = red;
  root_red.index = 0U;
  prism::SetSingleColor root_blue;
  root_blue.mark = 200U;
  root_blue.color = blue;
  root_blue.index = 0U;
  prism::RunGroupPayload child_call_payload{};
  child_call_payload.group_id = 0U;
  child_call_payload.mark = 300U;
  child_call_payload.target_id = 0x80000004U;
  child_call_payload.additional_repeats = 1U;
  prism::RunGroupInstruction child_call{child_call_payload};
  prism::SetSingleColor child_yellow;
  child_yellow.group_id = child_call_payload.target_id;
  child_yellow.color = yellow;
  child_yellow.index = 1U;
  prism::SetSingleColor child_green;
  child_green.group_id = child_call_payload.target_id;
  child_green.mark = 100U;
  child_green.color = green;
  child_green.index = 1U;
  prism::test::TimedInstruction delay{100U, &delay_calls};
  delay.group_id = child_call_payload.target_id;
  delay.mark = 100U;
  prism::SetSingleColor final_root_red;
  final_root_red.mark = 500U;
  final_root_red.color = red;
  final_root_red.index = 0U;

  ASSERT_EQ(controller_.AddInstruction(&root_red),
            prism::ControllerStatus::kSuccess);
  ASSERT_EQ(controller_.AddInstruction(&root_blue),
            prism::ControllerStatus::kSuccess);
  ASSERT_EQ(controller_.AddInstruction(&child_call),
            prism::ControllerStatus::kSuccess);
  ASSERT_EQ(controller_.AddInstruction(&child_yellow),
            prism::ControllerStatus::kSuccess);
  ASSERT_EQ(controller_.AddInstruction(&child_green),
            prism::ControllerStatus::kSuccess);
  ASSERT_EQ(controller_.AddInstruction(&delay),
            prism::ControllerStatus::kSuccess);
  ASSERT_EQ(controller_.AddInstruction(&final_root_red),
            prism::ControllerStatus::kSuccess);
  ASSERT_EQ(controller_.Start(), prism::ControllerStatus::kSuccess);

  auto* root_led = mock_strip_.MutableLed(0U);
  auto* child_led = mock_strip_.MutableLed(1U);
  EXPECT_CALL(*root_led, SetColor(testing::_)).Times(0);
  EXPECT_CALL(*child_led, SetColor(testing::_)).Times(0);
  EXPECT_CALL(mock_strip_, Show()).Times(0);
  EXPECT_EQ(controller_.Run(), prism::ControllerStatus::kSuccess);
  EXPECT_EQ(g_last_scheduled_delay, 100U);
  testing::Mock::VerifyAndClearExpectations(&mock_strip_);

  g_fake_time = 100U;
  g_last_scheduled_delay = 0U;
  EXPECT_CALL(*root_led, SetColor(red)).Times(1);
  EXPECT_CALL(*child_led, SetColor(testing::_)).Times(0);
  EXPECT_CALL(mock_strip_, Show()).Times(1);
  EXPECT_EQ(controller_.Run(), prism::ControllerStatus::kSuccess);
  EXPECT_EQ(g_last_scheduled_delay, 100U);
  testing::Mock::VerifyAndClearExpectations(&mock_strip_);

  g_fake_time = 200U;
  g_last_scheduled_delay = 0U;
  EXPECT_CALL(*root_led, SetColor(blue)).Times(1);
  EXPECT_CALL(*child_led, SetColor(testing::_)).Times(0);
  EXPECT_CALL(mock_strip_, Show()).Times(1);
  EXPECT_EQ(controller_.Run(), prism::ControllerStatus::kSuccess);
  EXPECT_EQ(g_last_scheduled_delay, 100U);
  testing::Mock::VerifyAndClearExpectations(&mock_strip_);

  g_fake_time = 300U;
  g_last_scheduled_delay = 0U;
  EXPECT_CALL(*root_led, SetColor(testing::_)).Times(0);
  EXPECT_CALL(*child_led, SetColor(yellow)).Times(1);
  EXPECT_CALL(mock_strip_, Show()).Times(1);
  EXPECT_EQ(controller_.Run(), prism::ControllerStatus::kSuccess);
  EXPECT_EQ(g_last_scheduled_delay, 100U);
  testing::Mock::VerifyAndClearExpectations(&mock_strip_);

  g_fake_time = 400U;
  g_last_scheduled_delay = 0U;
  EXPECT_CALL(*root_led, SetColor(testing::_)).Times(0);
  EXPECT_CALL(*child_led, SetColor(green)).Times(1);
  EXPECT_CALL(mock_strip_, Show()).Times(1);
  EXPECT_EQ(controller_.Run(), prism::ControllerStatus::kSuccess);
  EXPECT_EQ(delay_calls, 1U);
  EXPECT_EQ(g_last_scheduled_delay, 100U);
  testing::Mock::VerifyAndClearExpectations(&mock_strip_);

  g_fake_time = 500U;
  g_last_scheduled_delay = 0U;
  testing::InSequence child_before_parent;
  EXPECT_CALL(*child_led, SetColor(yellow)).Times(1);
  EXPECT_CALL(*root_led, SetColor(red)).Times(1);
  EXPECT_CALL(mock_strip_, Show()).Times(1);
  EXPECT_EQ(controller_.Run(), prism::ControllerStatus::kSuccess);
  EXPECT_EQ(delay_calls, 2U);
  EXPECT_EQ(g_last_scheduled_delay, 100U);
  testing::Mock::VerifyAndClearExpectations(&mock_strip_);

  g_fake_time = 600U;
  g_last_scheduled_delay = 0U;
  EXPECT_CALL(*root_led, SetColor(testing::_)).Times(0);
  EXPECT_CALL(*child_led, SetColor(green)).Times(1);
  EXPECT_CALL(mock_strip_, Show()).Times(1);
  EXPECT_EQ(controller_.Run(), prism::ControllerStatus::kSuccess);
  EXPECT_EQ(delay_calls, 3U);
  EXPECT_EQ(g_last_scheduled_delay, 100U);
  testing::Mock::VerifyAndClearExpectations(&mock_strip_);

  g_fake_time = 700U;
  g_last_scheduled_delay = 0U;
  EXPECT_CALL(*root_led, SetColor(testing::_)).Times(0);
  EXPECT_CALL(*child_led, SetColor(testing::_)).Times(0);
  EXPECT_CALL(mock_strip_, Show()).Times(0);
  EXPECT_EQ(controller_.Run(), prism::ControllerStatus::kSuccess);
  EXPECT_EQ(delay_calls, 4U);
  EXPECT_EQ(g_last_scheduled_delay, 0U);
}

/// @brief Wrapped marks and timed deadlines remain ordered within half range.
TEST_F(ControllerTest, TimestampWrapPreservesMarksAndDeadlines) {
  std::uint32_t execution_count = 0U;
  prism::test::TimedInstruction timed{10U, &execution_count};
  prism::SetSingleColor marked;
  marked.mark = 5U;
  marked.index = 4U;

  ASSERT_EQ(controller_.AddInstruction(&timed),
            prism::ControllerStatus::kSuccess);
  ASSERT_EQ(controller_.AddInstruction(&marked),
            prism::ControllerStatus::kSuccess);
  g_fake_time = 0xFFFFFFFAU;
  ASSERT_EQ(controller_.Start(), prism::ControllerStatus::kSuccess);

  EXPECT_CALL(*mock_strip_.MutableLed(4U), SetColor(testing::_)).Times(0);
  EXPECT_CALL(mock_strip_, Show()).Times(0);
  EXPECT_EQ(controller_.Run(), prism::ControllerStatus::kSuccess);
  EXPECT_EQ(execution_count, 1U);
  EXPECT_EQ(g_last_scheduled_delay, 5U);
  testing::Mock::VerifyAndClearExpectations(&mock_strip_);

  g_fake_time = 0xFFFFFFFFU;
  g_last_scheduled_delay = 0U;
  EXPECT_CALL(*mock_strip_.MutableLed(4U), SetColor(testing::_)).Times(1);
  EXPECT_CALL(mock_strip_, Show()).Times(1);
  EXPECT_EQ(controller_.Run(), prism::ControllerStatus::kSuccess);
  EXPECT_EQ(execution_count, 1U);
  EXPECT_EQ(g_last_scheduled_delay, 5U);
  testing::Mock::VerifyAndClearExpectations(&mock_strip_);

  g_fake_time = 4U;
  g_last_scheduled_delay = 0U;
  EXPECT_CALL(mock_strip_, Show()).Times(0);
  EXPECT_EQ(controller_.Run(), prism::ControllerStatus::kSuccess);
  EXPECT_EQ(execution_count, 2U);
  EXPECT_EQ(g_last_scheduled_delay, 0U);
}

/// @brief Reset aborts active children and makes stale timer ticks harmless.
TEST_F(ControllerTest, ResetAbortsChildTimedWorkAndStaleTick) {
  std::uint32_t child_calls = 0U;
  prism::RunGroupPayload child_call_payload{};
  child_call_payload.group_id = 0U;
  child_call_payload.target_id = 0x81000002U;
  child_call_payload.additional_repeats = prism::kForeverLoopCount;
  prism::RunGroupInstruction child_call{child_call_payload};
  prism::test::TimedInstruction child_timed{10U, &child_calls};
  child_timed.group_id = child_call_payload.target_id;

  ASSERT_EQ(controller_.AddInstruction(&child_call),
            prism::ControllerStatus::kSuccess);
  ASSERT_EQ(controller_.AddInstruction(&child_timed),
            prism::ControllerStatus::kSuccess);
  ASSERT_EQ(controller_.Start(), prism::ControllerStatus::kSuccess);
  EXPECT_EQ(controller_.Run(), prism::ControllerStatus::kSuccess);
  EXPECT_EQ(child_calls, 1U);
  EXPECT_EQ(g_last_scheduled_delay, 10U);

  controller_.ResetInstructions();
  g_fake_time = 10U;
  g_last_scheduled_delay = 0U;
  EXPECT_CALL(mock_strip_, Show()).Times(0);
  EXPECT_EQ(controller_.Run(), prism::ControllerStatus::kSuccess);
  EXPECT_EQ(child_calls, 1U);
  EXPECT_EQ(g_last_scheduled_delay, 0U);
  EXPECT_EQ(controller_.AddInstruction(&child_timed),
            prism::ControllerStatus::kSuccess);
}

/// @brief A completed program replays from a fresh origin with reset state.
TEST_F(ControllerTest, CompletedProgramReplayUsesFreshOrigin) {
  std::uint32_t execution_count = 0U;
  prism::test::TimedInstruction timed{10U, &execution_count};
  ASSERT_EQ(controller_.AddInstruction(&timed),
            prism::ControllerStatus::kSuccess);
  g_fake_time = 100U;
  ASSERT_EQ(controller_.Start(), prism::ControllerStatus::kSuccess);
  EXPECT_EQ(controller_.Run(), prism::ControllerStatus::kSuccess);
  EXPECT_EQ(execution_count, 1U);

  g_fake_time = 110U;
  EXPECT_EQ(controller_.Run(), prism::ControllerStatus::kSuccess);
  EXPECT_EQ(execution_count, 2U);

  g_fake_time = 200U;
  g_last_scheduled_delay = 0U;
  ASSERT_EQ(controller_.Start(), prism::ControllerStatus::kSuccess);
  EXPECT_EQ(controller_.Run(), prism::ControllerStatus::kSuccess);
  EXPECT_EQ(execution_count, 3U);
  EXPECT_EQ(g_last_scheduled_delay, 10U);

  g_fake_time = 210U;
  g_last_scheduled_delay = 0U;
  EXPECT_EQ(controller_.Run(), prism::ControllerStatus::kSuccess);
  EXPECT_EQ(execution_count, 4U);
}

/// @brief Instructions sharing a mark retain their insertion order.
TEST_F(ControllerTest, EqualMarksPreserveInsertionOrder) {
  prism::SetSingleColor first;
  first.mark = 10U;
  first.color = prism::color::ToRgb(prism::color::Preset::kPureRed);
  first.index = 0U;
  prism::SetSingleColor second;
  second.mark = 10U;
  second.color = prism::color::ToRgb(prism::color::Preset::kPureBlue);
  second.index = 1U;

  ASSERT_EQ(controller_.AddInstruction(&first),
            prism::ControllerStatus::kSuccess);
  ASSERT_EQ(controller_.AddInstruction(&second),
            prism::ControllerStatus::kSuccess);
  ASSERT_EQ(controller_.Start(), prism::ControllerStatus::kSuccess);
  EXPECT_EQ(controller_.Run(), prism::ControllerStatus::kSuccess);
  EXPECT_EQ(g_last_scheduled_delay, 10U);

  g_fake_time = 10U;
  auto* first_led = mock_strip_.MutableLed(0U);
  auto* second_led = mock_strip_.MutableLed(1U);
  testing::InSequence sequence;
  EXPECT_CALL(*first_led,
              SetColor(prism::color::ToRgb(prism::color::Preset::kPureRed)))
    .Times(1);
  EXPECT_CALL(*second_led,
              SetColor(prism::color::ToRgb(prism::color::Preset::kPureBlue)))
    .Times(1);
  EXPECT_CALL(mock_strip_, Show()).Times(1);
  EXPECT_EQ(controller_.Run(), prism::ControllerStatus::kSuccess);
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
