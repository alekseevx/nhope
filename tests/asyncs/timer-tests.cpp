#include <chrono>
#include <memory>
#include <thread>

#include <nhope/asyncs/ao-context.h>
#include <nhope/asyncs/thread-executor.h>
#include <nhope/asyncs/timer.h>

#include <gtest/gtest.h>

using namespace nhope::asyncs;
using namespace std::literals;

using time_point = std::chrono::steady_clock::time_point;

TEST(SetTimeout, AsyncWait)   // NOLINT
{
    auto executor = ThreadExecutor();
    auto aoCtx = AOContext(executor);

    const time_point startTime = std::chrono::steady_clock::now();
    time_point stopTime;

    setTimeout(aoCtx, 1000ms, [&stopTime](const std::error_code& code) {
        GTEST_CHECK_(!code) << code;
        stopTime = std::chrono::steady_clock::now();
    });

    std::this_thread::sleep_for(1250ms);

    GTEST_CHECK_(stopTime != time_point()) << "The timer was not triggered";

    const auto duration = stopTime - startTime;
    GTEST_CHECK_(duration >= 1000ms && duration < 1100ms) << "The timer worket at the wrong time";
}

TEST(SetTimeout, Canceling)   // NOLINT
{
    auto executor = ThreadExecutor();

    bool cancelableTimerTriggered = false;
    auto cancelableTimerAOCtx = std::make_unique<AOContext>(executor);
    setTimeout(*cancelableTimerAOCtx, 1250ms, [&cancelableTimerTriggered](const std::error_code& /*unused*/) {
        cancelableTimerTriggered = true;
    });

    auto timerAOCtx = AOContext(executor);
    setTimeout(timerAOCtx, 1000ms, [&cancelableTimerAOCtx](const std::error_code& code) {
        GTEST_CHECK_(!code) << code;
        cancelableTimerAOCtx.reset();
    });

    std::this_thread::sleep_for(1250ms);

    ASSERT_FALSE(cancelableTimerTriggered);
}
