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

    ASSERT_TRUE(task->asyncWaitForStopped().waitFor(1s));
    ASSERT_EQ(task->state(), ManageableTask::State::Stopped);

    /* Повторный вызов допустим asyncWaitForStopped. */
    ASSERT_TRUE(task->asyncWaitForStopped().waitFor(0s));
}

static bool isChanged(std::atomic<int>& value)
{
    using namespace std::chrono;

    int currentValue = value.load();
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
        auto pauseSecondFuture = task->asyncPause();
        auto taskState = task->state();
        ASSERT_TRUE(taskState == ManageableTask::State::Pausing || taskState == ManageableTask::State::Paused);

        ASSERT_TRUE(pauseFuture.waitFor(100ms));
        ASSERT_EQ(task->state(), ManageableTask::State::Paused);
        ASSERT_TRUE(pauseSecondFuture.waitFor(1ms));
        pauseSecondFuture.get();
        ASSERT_FALSE(isChanged(counter));

        auto resumeFuture = task->asyncResume();
        ASSERT_TRUE(resumeFuture.waitFor(100ms));
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

    ASSERT_TRUE(task->asyncPause().waitFor(1s));
    auto taskState = task->state();
    ASSERT_TRUE(taskState == ManageableTask::State::Paused);
    ASSERT_FALSE(isChanged(counter));

    task->asyncStop();
    ASSERT_TRUE(task->asyncWaitForStopped().waitFor(1s));
    ASSERT_EQ(task->state(), ManageableTask::State::Stopped);
    auto pause = task->asyncPause();
    ASSERT_TRUE(pause.isReady());
    pause.get();
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
    task->asyncPause();

    task->resume();
    ASSERT_EQ(task->state(), ManageableTask::State::Running);

    pauseEnable = false;
    auto pauseFuture = task->asyncPause();
    ASSERT_FALSE(pauseFuture.waitFor(10ms));
    ASSERT_EQ(task->state(), ManageableTask::State::Pausing);

    pauseEnable = true;
    ASSERT_TRUE(pauseFuture.waitFor(100ms));
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

    ASSERT_EQ(task->state(), ManageableTask::State::Waiting);

    ASSERT_EQ(counter, 0);
    task->resume();
    ASSERT_EQ(task->state(), ManageableTask::State::Running);

    task->waitForStopped();
    ASSERT_EQ(counter, 4);
    ASSERT_EQ(task->state(), ManageableTask::State::Stopped);
}

TEST(ManageableTask, Exception)   // NOLINT
{
    static constexpr auto failCounter{100};
    auto task = ManageableTask::start([counter = 0](nhope::ManageableTaskCtx& ctx) mutable {
        ctx.setAfterPause([] {
            FAIL() << "handler was be resetted";
        });

        while (ctx.checkPoint()) {
            ctx.setAfterPause(nullptr);
            if (++counter == failCounter) {
                throw std::invalid_argument("something go wrong");
            }
            std::this_thread::sleep_for(1ms);
        }
    });
    std::this_thread::sleep_for(10ms);

    task->pause();
    task->resume();
    task->waitForStopped();
    EXPECT_THROW(std::rethrow_exception(task->getError()), std::invalid_argument);   //NOLINT
}
