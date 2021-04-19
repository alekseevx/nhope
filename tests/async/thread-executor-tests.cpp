#include <atomic>
#include <chrono>
#include <cstddef>
#include <thread>

#include <nhope/async/thread-executor.h>

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

TEST(ThreadExecution, Execution)   // NOLINT
{
    constexpr auto taskCount = 100;
    ThreadExecutor executor;

    EXPECT_NE(&executor.ioCtx(), nullptr);

    const auto executorThreadId = executor.id();

    std::atomic<int> finishedTaskCount = 0;

    for (int i = 0; i < taskCount; ++i) {
        executor.post([&] {
            EXPECT_EQ(executorThreadId, std::this_thread::get_id());
            ++finishedTaskCount;
        });
    }

    EXPECT_TRUE(wait(finishedTaskCount, taskCount, 100ms));
}
