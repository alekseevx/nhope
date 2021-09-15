#include <atomic>
#include <chrono>
#include <memory>
#include <thread>

#include <gtest/gtest.h>

#include <nhope/async/thread-executor.h>
#include <nhope/seq/notifier.h>
#include <nhope/seq/func-producer.h>

#include "nhope/async/ao-context.h"
#include "test-helpers/wait.h"

using namespace nhope;
using namespace std::chrono_literals;

TEST(NotifierTests, CallHandler)   // NOLINT
{
    static constexpr int iterCount = 100;

    FuncProducer<int> numProducer([n = 0](int& value) mutable -> bool {
        value = n;
        return ++n < iterCount;
    });

    std::atomic<int> counter = 0;
    ThreadExecutor executor;
    AOContext aoCtx(executor);
    Notifier<int> notifier(aoCtx, [&counter](const int& v) {
        EXPECT_EQ(counter++, v);
    });
    notifier.attachToProducer(numProducer);
    numProducer.start();

    EXPECT_TRUE(waitForValue(1s, counter, iterCount - 1));
}

TEST(NotifierTests, CreateDestroy)   // NOLINT
{
    static constexpr int iterCount = 100;

    FuncProducer<int> numProducer([](int& value) -> bool {
        value = 0;
        return true;
    });
    numProducer.start();

    ThreadExecutor executor;
    AOContext aoCtx(executor);
    for (int i = 0; i < iterCount; ++i) {
        Notifier<int> notifier(aoCtx, [](const int& /*unused*/) {});
        notifier.attachToProducer(numProducer);

        const auto sleepTime = (i % 5) * 1ms;
        std::this_thread::sleep_for(sleepTime);
    }
}

TEST(NotifierTests, DestroyFromHandler)   // NOLINT
{
    FuncProducer<int> numProducer([](int& value) -> bool {
        value = 0;
        return true;
    });
    numProducer.start();

    std::atomic<bool> destroyed = false;
    ThreadExecutor executor;
    AOContext aoCtx(executor);

    std::unique_ptr<Notifier<int>> notifier;
    notifier = std::make_unique<Notifier<int>>(aoCtx, [&](const int& /*unused*/) {
        EXPECT_FALSE(destroyed);

        notifier.reset();
        destroyed = true;
    });

    notifier->attachToProducer(numProducer);
    waitForValue(1s, destroyed, true);

    /* Let's wait to make sure that the notifier handler is no longer called */
    std::this_thread::sleep_for(200ms);
}
