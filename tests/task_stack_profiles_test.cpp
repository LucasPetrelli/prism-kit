#include "task/task_stack_profiles.hpp"

#include <array>
#include <atomic>
#include <cstddef>
#include <thread>

#include "gtest/gtest.h"

namespace {

constexpr std::array<std::size_t, 2U> kProfileSizes{1024U, 1536U};
constexpr std::size_t kMaximumStackSizeBytes = 1536U;

using oshal::internal::CreateTaskWithStackProfile;
using oshal::internal::ReleaseTaskStackProfile;
using oshal::internal::TaskStackProfileCreateStatus;
using oshal::internal::TaskStackProfileReleaseStatus;
using oshal::internal::TaskThreadJoinStatus;

TEST(TaskStackProfilesTest, MatchesCallerSizesInEitherCreationOrder) {
  // Goal: Ensure stack selection depends on capacity, not task creation order.
  // Steps: Create 1,536/1,024-byte tasks in both orders and check each profile.
  const std::array<std::array<std::size_t, 2U>, 2U> request_orders{{
    {{1536U, 1024U}},
    {{1024U, 1536U}},
  }};

  for (const auto& requests : request_orders) {
    std::array<bool, 2U> allocated{};
    std::array<std::size_t, 2U> selected_profiles{};

    for (std::size_t task_index = 0U; task_index < requests.size();
         ++task_index) {
      const auto result = CreateTaskWithStackProfile(
        kProfileSizes, requests[task_index], kMaximumStackSizeBytes,
        [&allocated](std::size_t profile_index) {
          if (allocated[profile_index]) {
            return false;
          }
          allocated[profile_index] = true;
          return true;
        },
        [](std::size_t) { return true; }, [](std::size_t) {});

      ASSERT_EQ(result.status, TaskStackProfileCreateStatus::kCreated);
      selected_profiles[task_index] = result.profile_index;
    }

    EXPECT_EQ(selected_profiles[0U], requests[0U] == 1536U ? 1U : 0U);
    EXPECT_EQ(selected_profiles[1U], requests[1U] == 1536U ? 1U : 0U);
  }
}

TEST(TaskStackProfilesTest, SmallRequestFallsBackAndExhaustionIsReported) {
  // Goal: Select the smallest free fit without assigning an undersized stack.
  // Steps: Occupy the 1,024-byte profile, fall back to 1,536 bytes, then
  // exhaust both profiles and verify the final request is unavailable.
  std::array<bool, 2U> allocated{};
  const auto try_create = [&allocated]() {
    return CreateTaskWithStackProfile(
      kProfileSizes, 1024U, kMaximumStackSizeBytes,
      [&allocated](std::size_t profile_index) {
        if (allocated[profile_index]) {
          return false;
        }
        allocated[profile_index] = true;
        return true;
      },
      [](std::size_t) { return true; }, [](std::size_t) {});
  };

  const auto first = try_create();
  const auto second = try_create();
  const auto third = try_create();

  EXPECT_EQ(first.status, TaskStackProfileCreateStatus::kCreated);
  EXPECT_EQ(first.profile_index, 0U);
  EXPECT_EQ(second.status, TaskStackProfileCreateStatus::kCreated);
  EXPECT_EQ(second.profile_index, 1U);
  EXPECT_EQ(third.status, TaskStackProfileCreateStatus::kUnavailable);
  EXPECT_EQ(third.profile_index,
            oshal::internal::kInvalidTaskStackProfileIndex);
}

TEST(TaskStackProfilesTest, ConcurrentCreatorsReserveDistinctProfiles) {
  // Goal: Ensure concurrent compatible requests cannot claim one profile twice.
  // Steps: Race two 1,024-byte creates against atomic profile reservations and
  // check that both succeed on different profile indices.
  std::array<std::atomic_bool, 2U> allocated{};
  for (auto& profile : allocated) {
    profile.store(false, std::memory_order_relaxed);
  }
  oshal::internal::TaskStackProfileCreateResult first_result{};
  oshal::internal::TaskStackProfileCreateResult second_result{};

  const auto create = [&allocated]() {
    return CreateTaskWithStackProfile(
      kProfileSizes, 1024U, kMaximumStackSizeBytes,
      [&allocated](std::size_t profile_index) {
        bool expected = false;
        return allocated[profile_index].compare_exchange_strong(
          expected, true, std::memory_order_acq_rel);
      },
      [](std::size_t) { return true; }, [](std::size_t) {});
  };

  std::thread first_creator([&]() { first_result = create(); });
  std::thread second_creator([&]() { second_result = create(); });
  first_creator.join();
  second_creator.join();

  ASSERT_EQ(first_result.status, TaskStackProfileCreateStatus::kCreated);
  ASSERT_EQ(second_result.status, TaskStackProfileCreateStatus::kCreated);
  EXPECT_NE(first_result.profile_index, second_result.profile_index);
}

TEST(TaskStackProfilesTest, InvalidRequestsDoNotReserveProfiles) {
  // Goal: Reject zero and over-maximum stack sizes at the backend boundary.
  // Steps: Attempt both invalid sizes and verify reservation callbacks are not
  // invoked; then confirm the smallest valid request selects the small profile.
  std::array<bool, 2U> allocated{};
  std::size_t reserve_attempts = 0U;
  const auto try_create = [&](std::size_t requested_size_bytes) {
    return CreateTaskWithStackProfile(
      kProfileSizes, requested_size_bytes, kMaximumStackSizeBytes,
      [&allocated, &reserve_attempts](std::size_t profile_index) {
        ++reserve_attempts;
        if (allocated[profile_index]) {
          return false;
        }
        allocated[profile_index] = true;
        return true;
      },
      [](std::size_t) { return true; }, [](std::size_t) {});
  };

  const auto zero = try_create(0U);
  const auto too_large = try_create(kMaximumStackSizeBytes + 1U);
  const auto minimum = try_create(1U);

  EXPECT_EQ(zero.status, TaskStackProfileCreateStatus::kInvalidRequest);
  EXPECT_EQ(too_large.status, TaskStackProfileCreateStatus::kInvalidRequest);
  EXPECT_EQ(minimum.status, TaskStackProfileCreateStatus::kCreated);
  EXPECT_EQ(minimum.profile_index, 0U);
  EXPECT_EQ(reserve_attempts, 1U);
}

TEST(TaskStackProfilesTest, FailedCreateRollsBackForProfileReuse) {
  // Goal: Return a reserved profile to the pool when Zephyr rejects creation.
  // Steps: Fail the first backend-create callback, verify rollback clears its
  // reservation, then retry and confirm the same profile is reusable.
  std::array<bool, 2U> allocated{};
  bool fail_create = true;
  const auto try_create = [&]() {
    return CreateTaskWithStackProfile(
      kProfileSizes, 1024U, kMaximumStackSizeBytes,
      [&allocated](std::size_t profile_index) {
        if (allocated[profile_index]) {
          return false;
        }
        allocated[profile_index] = true;
        return true;
      },
      [&fail_create](std::size_t) {
        if (fail_create) {
          fail_create = false;
          return false;
        }
        return true;
      },
      [&allocated](std::size_t profile_index) {
        allocated[profile_index] = false;
      });
  };

  const auto failed = try_create();
  EXPECT_EQ(failed.status, TaskStackProfileCreateStatus::kBackendError);
  EXPECT_FALSE(allocated[0U]);
  EXPECT_FALSE(allocated[1U]);

  const auto retried = try_create();
  EXPECT_EQ(retried.status, TaskStackProfileCreateStatus::kCreated);
  EXPECT_EQ(retried.profile_index, 0U);
}

TEST(TaskStackProfilesTest, RunningTaskCannotReleaseItsProfile) {
  // Goal: Preserve the existing not-ready behavior for a running task.
  // Steps: Attempt release while running and verify neither join nor reclaim is
  // invoked.
  bool join_called = false;
  bool reclaim_called = false;

  const auto result = ReleaseTaskStackProfile(
    true,
    [&join_called]() {
      join_called = true;
      return TaskThreadJoinStatus::kJoined;
    },
    [&reclaim_called]() {
      reclaim_called = true;
      return true;
    });

  EXPECT_EQ(result, TaskStackProfileReleaseStatus::kNotReady);
  EXPECT_FALSE(join_called);
  EXPECT_FALSE(reclaim_called);
}

TEST(TaskStackProfilesTest, ProfileWaitsUntilZephyrConfirmsTermination) {
  // Goal: Keep an exited task's stack reserved until Zephyr reports it dead.
  // Steps: Return a non-terminated join result and verify release remains
  // not-ready without calling the slot-reclamation callback.
  bool reclaim_called = false;

  const auto result = ReleaseTaskStackProfile(
    false, []() { return TaskThreadJoinStatus::kNotTerminated; },
    [&reclaim_called]() {
      reclaim_called = true;
      return true;
    });

  EXPECT_EQ(result, TaskStackProfileReleaseStatus::kNotReady);
  EXPECT_FALSE(reclaim_called);
}

TEST(TaskStackProfilesTest, JoinErrorDoesNotReclaimTheProfile) {
  // Goal: Surface a Zephyr join failure without making the stack reusable.
  // Steps: Return a join error and verify backend failure is reported while
  // reclamation remains untouched.
  bool reclaim_called = false;

  const auto result = ReleaseTaskStackProfile(
    false, []() { return TaskThreadJoinStatus::kError; },
    [&reclaim_called]() {
      reclaim_called = true;
      return true;
    });

  EXPECT_EQ(result, TaskStackProfileReleaseStatus::kBackendError);
  EXPECT_FALSE(reclaim_called);
}

TEST(TaskStackProfilesTest, SuccessfulJoinAllowsReuseOfTheReclaimedProfile) {
  // Goal: Reuse a profile only after join and generation-checked reclamation.
  // Steps: Release the 1,024-byte profile after join, clear its reservation,
  // then create another task and verify the same profile is selected.
  std::array<bool, 2U> allocated{true, false};
  const auto release_result = ReleaseTaskStackProfile(
    false, []() { return TaskThreadJoinStatus::kJoined; },
    [&allocated]() {
      if (!allocated[0U]) {
        return false;
      }
      allocated[0U] = false;
      return true;
    });
  const auto create_result = CreateTaskWithStackProfile(
    kProfileSizes, 1024U, kMaximumStackSizeBytes,
    [&allocated](std::size_t profile_index) {
      if (allocated[profile_index]) {
        return false;
      }
      allocated[profile_index] = true;
      return true;
    },
    [](std::size_t) { return true; }, [](std::size_t) {});

  EXPECT_EQ(release_result, TaskStackProfileReleaseStatus::kReleased);
  EXPECT_EQ(create_result.status, TaskStackProfileCreateStatus::kCreated);
  EXPECT_EQ(create_result.profile_index, 0U);
}

TEST(TaskStackProfilesTest, StaleGenerationCannotReclaimAReusedSlot) {
  // Goal: Prevent a stale handle from releasing a newer task's profile.
  // Steps: Confirm thread termination but reject the generation check and
  // verify release reports an invalid handle.
  bool reclaim_called = false;

  const auto result = ReleaseTaskStackProfile(
    false, []() { return TaskThreadJoinStatus::kJoined; },
    [&reclaim_called]() {
      reclaim_called = true;
      return false;
    });

  EXPECT_EQ(result, TaskStackProfileReleaseStatus::kInvalidHandle);
  EXPECT_TRUE(reclaim_called);
}

}  // namespace
