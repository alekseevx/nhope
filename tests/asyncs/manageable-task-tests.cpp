#include <atomic>
#include <chrono>
#include <thread>

#include <gtest/gtest.h>

#include <nhope/asyncs/manageable-task.h>

using namespace nhope::asyncs;
using namespace std::literals;

TEST(ManageableTask, checkAsyncWaitForStopped)
{
    auto task = ManageableTask::start([](auto&) {
        std::this_thread::sleep_for(500ms);
    });

    ASSERT_EQ(task->state(), ManageableTask::State::Running);

    auto status = task->asyncWaitForStopped().wait_for(1s);
    ASSERT_EQ(status, std::future_status::ready);
    ASSERT_EQ(task->state(), ManageableTask::State::Stopped);

    /* Повторный вызов допустим asyncWaitForStopped. */
    status = task->asyncWaitForStopped().wait_for(0s);
    ASSERT_EQ(status, std::future_status::ready);
}

static bool isChanged(std::atomic<int>& value)
{
    using namespace std::chrono;

    int currentValue = value;
    auto stop = steady_clock::now() + 10ms;
    while (steady_clock::now() < stop) {
        if (currentValue != value)
            return true;
    }

    return false;
}

TEST(ManageableTask, checkAsyncPauseResume)
{
    std::atomic<int> counter = 0;

    auto task = ManageableTask::start([&counter](auto& ctx) {
        while (ctx.checkPoint()) {
            if (++counter % 1000 == 0) {
                std::this_thread::sleep_for(1ms);
            }
        }
    });

    for (int i = 0; i < 100; ++i) {
        ASSERT_EQ(task->state(), ManageableTask::State::Running);
        ASSERT_TRUE(isChanged(counter));

        auto pauseFuture = task->asyncPause();
        auto taskState = task->state();
        ASSERT_TRUE(taskState == ManageableTask::State::Pausing || taskState == ManageableTask::State::Paused);

        ASSERT_EQ(pauseFuture.wait_for(10ms), std::future_status::ready);
        ASSERT_EQ(task->state(), ManageableTask::State::Paused);
        ASSERT_FALSE(isChanged(counter));

        auto resumeFuture = task->asyncResume();
        ASSERT_EQ(resumeFuture.wait_for(10ms), std::future_status::ready);
    }

    task->stop();
    ASSERT_EQ(task->state(), ManageableTask::State::Stopped);
    ASSERT_FALSE(isChanged(counter));
}

TEST(ManageableTask, checkStopPausedTask)
{
    std::atomic<int> counter = 0;

    auto task = ManageableTask::start([&counter](auto& ctx) {
        while (ctx.checkPoint()) {
            if (++counter % 1000 == 0) {
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
    auto status = task->asyncWaitForStopped().wait_for(1s);
    ASSERT_EQ(status, std::future_status::ready);
    ASSERT_EQ(task->state(), ManageableTask::State::Stopped);
    ASSERT_FALSE(isChanged(counter));
}

TEST(ManageableTask, checkEnableDisablePause)
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
    ASSERT_EQ(pauseFuture.wait_for(10ms), std::future_status::timeout);
    ASSERT_EQ(task->state(), ManageableTask::State::Pausing);

    pauseEnable = true;
    ASSERT_EQ(pauseFuture.wait_for(10ms), std::future_status::ready);
    ASSERT_EQ(task->state(), ManageableTask::State::Paused);
}
