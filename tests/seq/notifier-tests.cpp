#include <atomic>
#include <chrono>
#include <memory>
#include <thread>

#include <gtest/gtest.h>

#include <nhope/async/thread-executor.h>
#include <nhope/seq/notifier.h>
#include <nhope/seq/func-produser.h>

#include "test-helpers/wait.h"

using namespace nhope;
using namespace std::chrono_literals;

TEST(NotifierTests, CallHandler)   // NOLINT
{
    static constexpr int iterCount = 100;

    FuncProduser<int> numProduser([n = 0](int& value) mutable -> bool {
        value = n;
        return ++n < iterCount;
    });

    std::atomic<int> counter = 0;
    ThreadExecutor executor;
    Notifier<int> notifier(executor, [&counter](const int& v) {
        EXPECT_EQ(counter++, v);
    });
    notifier.attachToProduser(numProduser);
    numProduser.start();

    EXPECT_TRUE(waitForValue(1s, counter, iterCount - 1));
}

TEST(NotifierTests, CreateDestroy)   // NOLINT
{
    static constexpr int iterCount = 100;

    FuncProduser<int> numProduser([](int& value) -> bool {
        value = 0;
        return true;
    });
    numProduser.start();

    ThreadExecutor executor;
    for (int i = 0; i < iterCount; ++i) {
        Notifier<int> notifier(executor, [](const int& /*unused*/) {});
        notifier.attachToProduser(numProduser);

        const auto sleepTime = (i % 5) * 1ms;
        std::this_thread::sleep_for(sleepTime);
    }
}

TEST(NotifierTests, DestroyFromHandler)   // NOLINT
{
    FuncProduser<int> numProduser([](int& value) -> bool {
        value = 0;
        return true;
    });
    numProduser.start();

    std::atomic<bool> destroyed = false;
    ThreadExecutor executor;

    std::unique_ptr<Notifier<int>> notifier;
    notifier = std::make_unique<Notifier<int>>(executor, [&](const int& /*unused*/) {
        EXPECT_FALSE(destroyed);

        notifier.reset();
        destroyed = true;
    });

    notifier->attachToProduser(numProduser);
    waitForValue(1s, destroyed, true);

    /* Let's wait to make sure that the notifier handler is no longer called */
    std::this_thread::sleep_for(200ms);
}
