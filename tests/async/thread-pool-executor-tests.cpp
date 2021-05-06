#include <atomic>
#include <chrono>
#include <cstddef>
#include <thread>

#include <gtest/gtest.h>

#include <nhope/async/thread-pool-executor.h>

#include "test-helpers/wait.h"

using namespace nhope;
using namespace std::chrono_literals;

TEST(ThreadPoolExecutor, CreateDestroy)   // NOLINT
{
    constexpr std::size_t threadCount = 10;
    ThreadPoolExecutor executor(threadCount);
    EXPECT_NE(&executor.ioCtx(), nullptr);
    EXPECT_EQ(executor.threadCount(), threadCount);
}

TEST(ThreadPoolExecutor, CheckDefaultExecutor)   // NOLINT
{
    auto& executor = ThreadPoolExecutor::defaultExecutor();
    EXPECT_EQ(executor.threadCount(), std::thread::hardware_concurrency());
}

TEST(ThreadPoolExecutor, ParallelExecution)   // NOLINT
{
    constexpr std::size_t threadCount = 10;
    ThreadPoolExecutor executor(threadCount);

    const auto taskCount = executor.threadCount();

    std::atomic<std::size_t> activeTaskCount = taskCount;
    for (std::size_t i = 0; i < taskCount; ++i) {
        executor.post([&] {
            std::this_thread::sleep_for(100ms);
            --activeTaskCount;
        });
    }

    EXPECT_TRUE(waitForValue<std::size_t>(taskCount * 100ms / 2, activeTaskCount, 0));
}
