#include <chrono>

#include <nhope/asyncs/timer.h>
#include <gtest/gtest.h>

using namespace nhope::asyncs;
using namespace std::literals;

using time_point = std::chrono::steady_clock::time_point;

TEST(AsioSteadyTimerImpl, AsyncWait)
{
    boost::asio::io_context ctx;

    const time_point startTime = std::chrono::steady_clock::now();
    time_point stopTime;

    auto timer = Timer::start(ctx, 1000ms, [&stopTime](const std::error_code& code, Timer&) {
        GTEST_CHECK_(!code) << code;
        stopTime = std::chrono::steady_clock::now();
    });

    ctx.run_for(5s);

    GTEST_CHECK_(stopTime != time_point()) << "The timer was not triggered";

    const auto duration = stopTime - startTime;
    GTEST_CHECK_(duration >= 1000ms && duration < 1100ms) << "The timer worket at the wrong time";
}

TEST(AsioSteadyTimerImpl, Canceling)
{
    boost::asio::io_context ctx;

    bool cancelableTimerTriggered = false;
    auto cancelableTimer = Timer::start(ctx, 5s, [&cancelableTimerTriggered](const std::error_code& code, Timer&) {
        cancelableTimerTriggered = true;
    });

    auto timer = Timer::start(ctx, 1s, [&cancelableTimer](const std::error_code& code, Timer&) {
        GTEST_CHECK_(!code) << code;
        cancelableTimer.reset();
    });

    ctx.run_for(5s);
    ASSERT_FALSE(cancelableTimerTriggered);
}
