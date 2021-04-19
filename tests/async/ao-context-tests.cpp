#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <stdexcept>
#include <thread>
#include <utility>

#include <nhope/async/ao-context.h>
#include <nhope/async/thread-executor.h>
#include <nhope/async/thread-pool-executor.h>

#include <gtest/gtest.h>

namespace {
using namespace nhope;
using namespace std::literals;

template<typename T>
bool wait(std::atomic<T>& var, T value, std::chrono::nanoseconds timeout)
{
    using clock = std::chrono::steady_clock;
    auto time = clock::now() + timeout;
    while (var.load() != value && clock::now() <= time) {
        std::this_thread::sleep_for(1ms);
    }

    return var == value;
}

template<typename Pred>
bool wait(std::chrono::nanoseconds timeout, Pred pred)
{
    using clock = std::chrono::steady_clock;
    auto time = clock::now() + timeout;

    while (!pred() && clock::now() <= time) {
        std::this_thread::sleep_for(1ms);
    }

    return pred();
}

}   // namespace

TEST(AOContext, AsyncOperation)   // NOLINT
{
    ThreadExecutor executor;
    AOContext aoContext(executor);

    std::atomic<bool> asyncOperationHandlerCalled = false;
    std::function asyncOperationHandler = [&] {
        EXPECT_EQ(std::this_thread::get_id(), executor.id());
        asyncOperationHandlerCalled = true;
    };

    std::thread([operationFinished = aoContext.newAsyncOperation(asyncOperationHandler, nullptr)] {
        // A thread performing an asynchronous operation
        operationFinished();
    }).detach();

    EXPECT_TRUE(wait(asyncOperationHandlerCalled, true, 1s));
}

TEST(AOContext, CancelAsyncOperation)   // NOLINT
{
    constexpr int iterCount = 500;

    ThreadExecutor executor;
    for (int i = 0; i < iterCount; ++i) {
        std::function<void()> operationFinished;
        std::atomic<bool> asyncOperationHandlerCalled = false;
        std::atomic<bool> cancelAsyncOperationCalled = false;

        auto aoContext = std::make_unique<AOContext>(executor);

        std::function asyncOperationHandler = [&asyncOperationHandlerCalled, &executor] {
            EXPECT_EQ(std::this_thread::get_id(), executor.id());
            asyncOperationHandlerCalled = true;
        };
        std::function cancelAsyncOperation = [&cancelAsyncOperationCalled] {
            cancelAsyncOperationCalled = true;
        };

        std::thread([operationFinished = aoContext->newAsyncOperation(asyncOperationHandler, cancelAsyncOperation)] {
            // A thread performing an asynchronous operation
            operationFinished();
        }).detach();

        std::this_thread::sleep_for(5us);

        aoContext.reset();   // Destroy the aoContex and cancel the asyncOperation

        EXPECT_TRUE(wait(1s, [&] {
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
        std::function asyncOperationHandler = [&, operationNum] {
            EXPECT_EQ(++activeHandlerCount, 1);
            std::this_thread::sleep_for(1ms);
            --activeHandlerCount;

            EXPECT_EQ(finishedHandlerCount, operationNum);
            ++finishedHandlerCount;
        };

        auto operationFinished = aoContext.newAsyncOperation(asyncOperationHandler, nullptr);
        operationFinished();
    }

    EXPECT_TRUE(wait(finishedHandlerCount, iterCount, 100 * 1ms * iterCount));
}

TEST(AOContext, ExceptionInAsyncOperationHandler)   // NOLINT
{
    constexpr auto iterCount = 100;

    ThreadExecutor executor;
    AOContext aoContext(executor);

    std::atomic<int> asyncOperationHandlerCalled = 0;
    for (int i = 0; i < iterCount; ++i) {
        std::function asyncOperationHandler = [&] {
            asyncOperationHandlerCalled++;
            throw std::runtime_error("TestException");
        };

        auto operationFinished = aoContext.newAsyncOperation(asyncOperationHandler, nullptr);
        operationFinished();
    }

    EXPECT_TRUE(wait(asyncOperationHandlerCalled, iterCount, 1s));
}

TEST(AOContext, ExceptionInCancelAsyncOperation)   // NOLINT
{
    ThreadExecutor executor;
    auto aoContext = std::make_unique<AOContext>(executor);

    std::function asyncOperationHandler = [] {};
    std::function cancelAsyncOperation = [] {
        throw std::runtime_error("TestException");
    };

    EXPECT_NO_THROW({   // NOLINT
        aoContext->newAsyncOperation(asyncOperationHandler, cancelAsyncOperation);
        aoContext.reset();
    });
}
