#include <nhope/async/ao-context.h>
#include <nhope/async/safe-callback.h>
#include <nhope/async/thread-executor.h>

#include <fmt/format.h>

#include <gtest/gtest.h>
#include "test-helpers/wait.h"

using namespace nhope;
using namespace std::literals;

TEST(CallSafeCallback, Call)   // NOLINT
{
    constexpr auto iterCount = 100;

    ThreadExecutor executor;
    AOContext aoContext(executor);

    std::atomic<int> callbackCalled = 0;
    const auto safeCallback = makeSafeCallback(aoContext, std::function([&](int arg1, const std::string& arg2) {
                                                   EXPECT_EQ(executor.id(), std::this_thread::get_id());
                                                   EXPECT_EQ(arg1, callbackCalled);
                                                   EXPECT_EQ(arg2, fmt::format("{}", callbackCalled));

                                                   ++callbackCalled;
                                               }));

    for (int i = 0; i < iterCount; ++i) {
        safeCallback(i, fmt::format("{}", i));
    }

    EXPECT_TRUE(waitForValue(1s, callbackCalled, iterCount));
}

TEST(CallSafeCallback, CallAfterDestroyAOContext)   // NOLINT
{
    constexpr auto iterCount = 100;

    ThreadExecutor executor;

    for (int i = 0; i < iterCount; ++i) {
        auto aoContext = std::make_unique<AOContext>(executor);
        std::atomic<bool> aoContextDestroyed = false;

        const auto safeCallback = makeSafeCallback(*aoContext, std::function([&] {
            EXPECT_EQ(executor.id(), std::this_thread::get_id());
            EXPECT_FALSE(aoContextDestroyed);
        }));

        auto callbackCaller = std::thread([safeCallback, &aoContextDestroyed] {
            for (;;) {
                try {
                    safeCallback();
                } catch (const AOContextClosed&) {
                    return;
                }
            }
        });

        const auto sleepTime = (i % 10) * 1ms;
        std::this_thread::sleep_for(sleepTime);

        aoContext.reset();
        aoContextDestroyed = true;
        callbackCaller.join();
    }
}
