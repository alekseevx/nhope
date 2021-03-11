#include "nhope/async/future.h"
#include <chrono>
#include <memory>
#include <thread>

#include <nhope/async/ao-context.h>
#include <nhope/async/thread-executor.h>
#include <nhope/async/timer.h>

#include <gtest/gtest.h>

using namespace nhope;
using namespace std::literals;

using time_point = std::chrono::steady_clock::time_point;

TEST(SetTimeout, CallbackWait)   // NOLINT
{
    auto executor = ThreadExecutor();
    auto aoCtx = AOContext(executor);

    const time_point startTime = std::chrono::steady_clock::now();
    time_point stopTime;

    setTimeout(aoCtx, 1000ms, [&stopTime](const std::error_code& code) {
        EXPECT_TRUE(!code) << code;
        stopTime = std::chrono::steady_clock::now();
    });

    std::this_thread::sleep_for(1250ms);

    EXPECT_TRUE(stopTime != time_point()) << "The timer was not triggered";

    const auto duration = stopTime - startTime;
    EXPECT_TRUE(duration >= 1000ms && duration < 1500ms) << "The timer worket at the wrong time";
}

TEST(SetTimeout, CallbackCancel)   // NOLINT
{
    auto executor = ThreadExecutor();

    bool timerTriggered = false;
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
