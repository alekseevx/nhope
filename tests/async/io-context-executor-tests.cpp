#include <atomic>
#include <chrono>
#include <thread>

#include <asio/io_context.hpp>
#include <asio/executor_work_guard.hpp>

#include <nhope/async/io-context-executor.h>
#include <gtest/gtest.h>

#include "test-helpers/wait.h"

using namespace nhope;
using namespace std::literals;

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

    EXPECT_TRUE(waitForValue(1s, counter, iterCount));

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

    EXPECT_TRUE(waitForValue(1s, counter, iterCount));

    ioCtx.stop();
    workThread.join();
}
