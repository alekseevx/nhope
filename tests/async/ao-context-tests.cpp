#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>

#include <nhope/async/ao-context.h>
#include <nhope/async/safe-callback.h>
#include <nhope/async/thread-executor.h>
#include <nhope/async/thread-pool-executor.h>

#include <fmt/format.h>
#include <gtest/gtest.h>

#include "test-helpers/wait.h"

namespace {

using namespace nhope;
using namespace std::literals;

class TestAOHandler final : public AOHandler
{
public:
    explicit TestAOHandler(std::function<void()> call, std::function<void()> cancel = nullptr)
      : m_call(std::move(call))
      , m_cancel(std::move(cancel))
    {}

    void call() override
    {
        if (m_call) {
            m_call();
        }
    }

    void cancel() override
    {
        if (m_cancel) {
            m_cancel();
        }
    }

private:
    std::function<void()> m_call;
    std::function<void()> m_cancel;
};

}   // namespace

TEST(AOContext, AsyncOperation)   // NOLINT
{
    ThreadExecutor executor;
    AOContext aoContext(executor);

    std::atomic<bool> asyncOperationHandlerCalled = false;
    auto aoHandler = std::make_unique<TestAOHandler>([&] {
        EXPECT_EQ(std::this_thread::get_id(), executor.id());
        asyncOperationHandlerCalled = true;
    });

    std::thread([callAOHandler = aoContext.putAOHandler(std::move(aoHandler))]() mutable {
        // A thread performing an asynchronous operation
        callAOHandler();
    }).detach();

    EXPECT_TRUE(waitForValue(1s, asyncOperationHandlerCalled, true));
}

TEST(AOContext, CancelAsyncOperation)   // NOLINT
{
    constexpr int iterCount = 500;

    for (int i = 0; i < iterCount; ++i) {
        std::atomic<bool> asyncOperationHandlerCalled = false;
        std::atomic<bool> cancelAsyncOperationCalled = false;

        auto executor = std::make_unique<ThreadExecutor>();
        auto aoContext = std::make_unique<AOContext>(*executor);

        auto aoHandler = std::make_unique<TestAOHandler>(
          [&asyncOperationHandlerCalled, &executor] {
              EXPECT_EQ(std::this_thread::get_id(), executor->id());
              asyncOperationHandlerCalled = true;
          },
          [&cancelAsyncOperationCalled] {
              cancelAsyncOperationCalled = true;
          });

        std::thread([callAOHandler = aoContext->putAOHandler(std::move(aoHandler))]() mutable {
            // A thread performing an asynchronous operation
            callAOHandler();
        }).detach();

        std::this_thread::sleep_for(5us);

        aoContext.reset();   // Destroy the aoContex and cancel the asyncOperation
        executor.reset();

        EXPECT_TRUE(waitForPred(1s, [&] {
            return asyncOperationHandlerCalled || cancelAsyncOperationCalled;
        }));

        EXPECT_FALSE(asyncOperationHandlerCalled && cancelAsyncOperationCalled);
    }
}

TEST(AOContext, SequentialHandlerCall)   // NOLINT
{
    constexpr auto iterCount = 100;

    constexpr auto executorThreadCount = 10;
    ThreadPoolExecutor executor(executorThreadCount);

    AOContext aoContext(executor);

    std::atomic<int> activeHandlerCount = 0;
    std::atomic<int> finishedHandlerCount = 0;

    for (int operationNum = 0; operationNum < iterCount; ++operationNum) {
        auto aoHandler = std::make_unique<TestAOHandler>([&, operationNum] {
            EXPECT_EQ(++activeHandlerCount, 1);
            std::this_thread::sleep_for(1ms);
            --activeHandlerCount;

            EXPECT_EQ(finishedHandlerCount, operationNum);
            ++finishedHandlerCount;
        });

        aoContext.callAOHandler(std::move(aoHandler));
    }

    EXPECT_TRUE(waitForValue(100 * 1ms * iterCount, finishedHandlerCount, iterCount));
}

TEST(AOContext, ExceptionInAOHandler)   // NOLINT
{
    constexpr auto iterCount = 100;

    ThreadExecutor executor;
    AOContext aoContext(executor);

    std::atomic<int> asyncOperationHandlerCalled = 0;
    for (int i = 0; i < iterCount; ++i) {
        auto aoHandler = std::make_unique<TestAOHandler>([&] {
            asyncOperationHandlerCalled++;
            throw std::runtime_error("TestException");
        });

        aoContext.callAOHandler(std::move(aoHandler));
    }

    EXPECT_TRUE(waitForValue(1s, asyncOperationHandlerCalled, iterCount));
}

TEST(AOContext, ExceptionInCancelAsyncOperation)   // NOLINT
{
    ThreadExecutor executor;
    auto aoContext = std::make_unique<AOContext>(executor);

    auto aoHandler = std::make_unique<TestAOHandler>(nullptr, [] {
        throw std::runtime_error("TestException");
    });

    EXPECT_NO_THROW({   // NOLINT
        [[maybe_unused]] auto call = aoContext->putAOHandler(std::move(aoHandler));
        aoContext.reset();
    });
}
