#include <atomic>
#include <chrono>
#include <thread>

#include <gtest/gtest.h>

#include <nhope/async/manageable-task.h>

using namespace nhope;
using namespace std::literals;

TEST(ManageableTask, checkAsyncWaitForStopped)   // NOLINT
{
    auto task = ManageableTask::start([](auto& /*unused*/) {
        std::this_thread::sleep_for(500ms);
    });

    ASSERT_EQ(task->state(), ManageableTask::State::Running);

    auto status = task->asyncWaitForStopped().waitFor(1s);
    ASSERT_EQ(status, FutureStatus::ready);
    ASSERT_EQ(task->state(), ManageableTask::State::Stopped);

    /* Повторный вызов допустим asyncWaitForStopped. */
    status = task->asyncWaitForStopped().waitFor(0s);
    ASSERT_EQ(status, FutureStatus::ready);
}

static bool isChanged(std::atomic<int>& value)
{
    using namespace std::chrono;

    int currentValue = value;
    auto stop = steady_clock::now() + 100ms;
    while (steady_clock::now() < stop) {
        if (currentValue != value) {
            return true;
        }
    }

    return false;
}

TEST(ManageableTask, checkAsyncPauseResume)   // NOLINT
{
    static constexpr int iterCount = 100;
    static constexpr int sleepInterval = 1000;

    std::atomic<int> counter = 0;

    auto task = ManageableTask::start([&counter](auto& ctx) {
        while (ctx.checkPoint()) {
            if (++counter % sleepInterval == 0) {
                std::this_thread::sleep_for(1ms);
            }
        }
    });

    for (int i = 0; i < iterCount; ++i) {
        ASSERT_EQ(task->state(), ManageableTask::State::Running);
        ASSERT_TRUE(isChanged(counter));

        auto pauseFuture = task->asyncPause();
        auto taskState = task->state();
        ASSERT_TRUE(taskState == ManageableTask::State::Pausing || taskState == ManageableTask::State::Paused);

        ASSERT_EQ(pauseFuture.waitFor(100ms), FutureStatus::ready);
        ASSERT_EQ(task->state(), ManageableTask::State::Paused);
        ASSERT_FALSE(isChanged(counter));

        auto resumeFuture = task->asyncResume();
        ASSERT_EQ(resumeFuture.waitFor(100ms), FutureStatus::ready);
    }

    task->stop();
    ASSERT_EQ(task->state(), ManageableTask::State::Stopped);
    ASSERT_FALSE(isChanged(counter));
}

TEST(ManageableTask, checkStopPausedTask)   // NOLINT
{
    static constexpr int sleepInterval = 1000;
    std::atomic<int> counter = 0;

    auto task = ManageableTask::start([&counter](auto& ctx) {
        while (ctx.checkPoint()) {
            if (++counter % sleepInterval == 0) {
                std::this_thread::sleep_for(1ms);
            }
        }
    });

    ASSERT_EQ(task->state(), ManageableTask::State::Running);
    ASSERT_TRUE(isChanged(counter));

    auto pauseFuture = task->asyncPause();
    auto taskState = task->state();
    ASSERT_TRUE(taskState == ManageableTask::State::Pausing || taskState == ManageableTask::State::Paused);
    ASSERT_FALSE(isChanged(counter));

    task->asyncStop();
    auto status = task->asyncWaitForStopped().waitFor(1s);
    ASSERT_EQ(status, FutureStatus::ready);
    ASSERT_EQ(task->state(), ManageableTask::State::Stopped);
    ASSERT_FALSE(isChanged(counter));
}

TEST(ManageableTask, checkEnableDisablePause)   // NOLINT
{
    std::atomic<bool> pauseEnable = true;

    auto task = ManageableTask::start([&pauseEnable](auto& ctx) {
        ctx.setBeforePause([&pauseEnable]() -> bool {
            return pauseEnable;
        });

        while (ctx.checkPoint()) {
            std::this_thread::sleep_for(1ms);
        }
    });

    task->pause();
    ASSERT_EQ(task->state(), ManageableTask::State::Paused);

    task->resume();
    ASSERT_EQ(task->state(), ManageableTask::State::Running);

    pauseEnable = false;
    auto pauseFuture = task->asyncPause();
    ASSERT_EQ(pauseFuture.waitFor(10ms), FutureStatus::timeout);
    ASSERT_EQ(task->state(), ManageableTask::State::Pausing);

    pauseEnable = true;
    ASSERT_EQ(pauseFuture.waitFor(100ms), FutureStatus::ready);
    ASSERT_EQ(task->state(), ManageableTask::State::Paused);
}

TEST(ManageableTask, createPausedThenStart)   // NOLINT
{
    std::atomic<int> counter = 0;

    auto task = ManageableTask::create([&counter](auto& ctx) {
        while (ctx.checkPoint()) {
            if (++counter == 4) {
                return;
            }
            std::this_thread::sleep_for(1ms);
        }
    });

    ASSERT_EQ(task->state(), ManageableTask::State::Paused);

    ASSERT_EQ(counter, 0);
    task->resume();

    task->waitForStopped();
    ASSERT_EQ(counter, 4);
    ASSERT_EQ(task->state(), ManageableTask::State::Stopped);
}