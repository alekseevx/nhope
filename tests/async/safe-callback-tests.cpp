#include "nhope/async/ao-context.h"
#include "nhope/async/safe-callback.h"
#include "nhope/async/thread-executor.h"

#include <atomic>
#include <fmt/format.h>

#include <gtest/gtest.h>
#include <memory>
#include "test-helpers/wait.h"

using namespace nhope;
using namespace std::literals;

TEST(CallSafeCallback, Call)   // NOLINT
{
    constexpr auto iterCount = 100;

    ThreadExecutor executor;
    AOContext aoContext(executor);

    std::atomic<int> callbackCalled = 0;
    const auto safeCallback = makeSafeCallback(AOContextRef(aoContext), [&](int arg1, const std::string& arg2) {
        EXPECT_EQ(executor.id(), std::this_thread::get_id());
        EXPECT_EQ(arg1, callbackCalled);
        EXPECT_EQ(arg2, fmt::format("{}", callbackCalled));

        ++callbackCalled;
    });

    for (int i = 0; i < iterCount; ++i) {
        safeCallback(i, fmt::format("{}", i));
    }

    EXPECT_TRUE(waitForValue(1s, callbackCalled, iterCount));
}

// https://gitlab.olimp.lan/alekseev/nhope/-/issues/8
// Оборачиваемый callback можно копировать только при создании safeCallback.
//
// callback может захватывать данные, которые будут уничтожены одновременно с уничтожением
// AOContext. Поэтому, либо мы должны  гарантировать, что не будет делать копию после уничтожения AOContext,
// либо мы должны вообще отказаться от копирования callback после создания SafeCallback.
// С учетом того, что потенциально копирование callback-а может быть дорогим, был выбран второй вариант.
TEST(CallSafeCallback, ProhibitingCopyingCallbackWhenCallSafeCallback)   // NOLINT
{
    class CopiedClass
    {
    public:
        CopiedClass() = default;

        CopiedClass(const CopiedClass& other)
        {
            EXPECT_TRUE(*other.copyingIsProhibited);
            copyingIsProhibited = other.copyingIsProhibited;
        }

        CopiedClass& operator=(const CopiedClass& /*unused*/) = delete;

        std::shared_ptr<bool> copyingIsProhibited = std::make_shared<bool>(true);
    };

    ThreadExecutor executor;
    AOContext aoCtx(executor);

    CopiedClass copiedObject;
    *copiedObject.copyingIsProhibited = true;   // При создании SafeCallback-а копирование callback-а разрешено

    std::atomic<bool> finished = false;
    auto callback = [&, copiedObject] {
        finished = true;
    };
    auto safeCallback = makeSafeCallback(aoCtx, callback);

    // safeCallback создан, копировать callback больше нельзя
    *copiedObject.copyingIsProhibited = false;

    // Вызываем callback и убеждаемся, что копирование callback выполнено не будет.
    safeCallback();
    waitForValue(10s, finished, true);
}

TEST(CallSafeCallback, AOContextClosedActions_ThrowAOContextClosed)   // NOLINT
{
    constexpr auto iterCount = 100;

    ThreadExecutor executor;

    for (int i = 0; i < iterCount; ++i) {
        auto aoContext = std::make_unique<AOContext>(executor);
        std::atomic<bool> aoContextDestroyed = false;

        const auto safeCallback = makeSafeCallback(*aoContext, [&] {
            EXPECT_EQ(executor.id(), std::this_thread::get_id());
            EXPECT_FALSE(aoContextDestroyed);
        });

        auto callbackCaller = std::thread([&] {
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

TEST(CallSafeCallback, AOContextClosedActions_NotThrowAOContextClosed)   // NOLINT
{
    ThreadExecutor executor;
    auto aoContext = std::make_unique<AOContext>(executor);

    const auto safeCallback = makeSafeCallback(
      *aoContext, [&] {}, nhope::NotThrowAOContextClosed);

    aoContext.reset();
    EXPECT_NO_THROW(safeCallback());   // NOLINT
}
