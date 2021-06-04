#include <atomic>
#include <chrono>
#include <memory>
#include <thread>

#include <nhope/async/ao-context.h>
#include <nhope/async/future.h>
#include <nhope/async/thread-executor.h>
#include <nhope/async/timer.h>

#include <gtest/gtest.h>

#include "test-helpers/wait.h"

using namespace nhope;
using namespace std::literals;

using time_point = std::chrono::steady_clock::time_point;

TEST(SetTimeout, CallbackWait)   // NOLINT
{
    auto executor = ThreadExecutor();
    auto aoCtx = AOContext(executor);

    const time_point startTime = std::chrono::steady_clock::now();
    std::atomic<time_point> stopTime;

    setTimeout(aoCtx, 1000ms, [&](const std::error_code& code) {
        EXPECT_TRUE(!code) << code;
        stopTime = std::chrono::steady_clock::now();
    });

    std::this_thread::sleep_for(1250ms);

    EXPECT_TRUE(stopTime.load() != time_point()) << "The timer was not triggered";

    const auto duration = stopTime.load() - startTime;
    EXPECT_TRUE(duration >= 1000ms && duration < 1500ms) << "The timer worked at the wrong time";
}

TEST(SetTimeout, CallbackCancel)   // NOLINT
{
    auto executor = ThreadExecutor();

    std::atomic<bool> timerTriggered = false;
    auto aoCtx = std::make_unique<AOContext>(executor);
    setTimeout(*aoCtx, 250ms, [&timerTriggered](const std::error_code& /*unused*/) {
        timerTriggered = true;
    });

    // Destroying the aoCtx must cancel all timers.
    aoCtx.reset();

    std::this_thread::sleep_for(500ms);

    EXPECT_FALSE(timerTriggered);
}

TEST(SetTimeout, FutureWait)   // NOLINT
{
    auto executor = ThreadExecutor();
    auto aoCtx = AOContext(executor);

    auto timeoutFuture = setTimeout(aoCtx, 250ms);
    EXPECT_TRUE(timeoutFuture.waitFor(500ms));
    EXPECT_NO_THROW(timeoutFuture.get());   // NOLINT
}

TEST(SetTimeout, FutureCancel)   // NOLINT
{
    auto executor = ThreadExecutor();

    auto aoCtx = std::make_unique<AOContext>(executor);
    auto timeoutFuture = setTimeout(*aoCtx, 250ms);

    // Destroying the aoCtx must cancel all timers.
    aoCtx.reset();

    EXPECT_TRUE(timeoutFuture.waitFor(500ms));
    EXPECT_THROW(timeoutFuture.get(), AsyncOperationWasCancelled);   // NOLINT
}

TEST(SetInterval, FourTicks)   // NOLINT
{
    static constexpr auto tickCount = 4;

    auto executor = ThreadExecutor();
    auto aoCtx = AOContext(executor);

    std::atomic<int> tickCounter = 0;
    const auto startTime = std::chrono::steady_clock::now();
    setInterval(aoCtx, 20ms, [&](const auto& err) {
        const auto tickTime = std::chrono::steady_clock::now();
        ++tickCounter;

        EXPECT_FALSE(err);
        EXPECT_LE(tickCounter, tickCount);
        EXPECT_GE(tickTime, startTime + tickCounter.load() * 20ms);

        return tickCounter < tickCount;
    });

    std::this_thread::sleep_for(4 * 20ms * tickCount);
    EXPECT_EQ(tickCounter, tickCount);
}

TEST(SetInterval, DestroyAOContex)   // NOLINT
{
    auto executor = ThreadExecutor();

    auto aoCtx = std::make_unique<AOContext>(executor);
    std::atomic<bool> timerTriggered = false;
    setInterval(*aoCtx, 250ms, [&](const auto& /*unused*/) {
        timerTriggered = true;
        return true;
    });

    // Destroying the aoCtx must cancel all timers.
    aoCtx.reset();

    std::this_thread::sleep_for(500ms);

    EXPECT_FALSE(timerTriggered);
}
