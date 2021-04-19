#include <atomic>
#include <chrono>
#include <cstddef>
#include <thread>

#include <nhope/async/thread-pool-executor.h>

#include <gtest/gtest.h>

namespace {
using namespace nhope;
using namespace std::chrono_literals;

bool wait(std::atomic<int>& var, int value, std::chrono::nanoseconds timeout)
{
    using clock = std::chrono::steady_clock;
    auto time = clock::now() + timeout;
    while (var != value && clock::now() < time) {
        std::this_thread::sleep_for(1ms);
    }

    return var == value;
}

}   // namespace

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

    std::atomic<int> activeTaskCount = taskCount;
    for (std::size_t i = 0; i < taskCount; ++i) {
        executor.post([&] {
            std::this_thread::sleep_for(100ms);
            --activeTaskCount;
        });
    }

    EXPECT_TRUE(wait(activeTaskCount, 0, taskCount * 100ms / 2));
}
