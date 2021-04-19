#include <atomic>
#include <chrono>
#include <thread>

#include <asio/io_context.hpp>
#include <asio/executor_work_guard.hpp>

#include <nhope/async/io-context-executor.h>
#include <gtest/gtest.h>

namespace {

using namespace nhope;
using namespace std::literals;

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

TEST(IOContextExecutor, Executor)   // NOLINT
{
    constexpr auto iterCount = 10;

    asio::io_context ioCtx;
    auto workThread = std::thread([&ioCtx] {
        auto workGuard = asio::make_work_guard(ioCtx);
        ioCtx.run();
    });

    IOContextExecutor executor(ioCtx);
    EXPECT_EQ(&ioCtx, &executor.ioCtx());

    std::atomic<int> counter = 0;
    for (int i = 0; i < iterCount; ++i) {
        executor.post([&counter] {
            ++counter;
        });
    }

    EXPECT_TRUE(wait(counter, iterCount, 1s));

    ioCtx.stop();
    workThread.join();
}

TEST(IOContextExecutor, SequenceExecutor)   // NOLINT
{
    constexpr auto iterCount = 10;

    asio::io_context ioCtx;
    auto workThread = std::thread([&ioCtx] {
        auto workGuard = asio::make_work_guard(ioCtx);
        ioCtx.run();
    });

    IOContextSequenceExecutor executor(ioCtx);
    EXPECT_EQ(&ioCtx, &executor.ioCtx());

    std::atomic<int> counter = 0;
    for (int i = 0; i < iterCount; ++i) {
        executor.post([&counter] {
            ++counter;
        });
    }

    EXPECT_TRUE(wait(counter, iterCount, 1s));

    ioCtx.stop();
    workThread.join();
}
