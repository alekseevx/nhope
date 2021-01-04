#include <chrono>
#include <cstdio>
#include <exception>
#include <stdexcept>
#include <string>
#include <thread>

#include <nhope/asyncs/ao-context.h>
#include <nhope/asyncs/async-invoke.h>
#include <nhope/asyncs/thread-executor.h>

#include <gtest/gtest.h>

namespace {

using namespace nhope;
using namespace std::literals;

std::string testArgValue(int invokeNum)
{
    return "test string " + std::to_string(invokeNum);
}

// Пример потокобезопасного класса
class TestClass final
{
public:
    explicit TestClass(ThreadExecutor& executor)
      : m_aoCtx(executor)
    {}

public:   // Функции, вызываемые из внешних потоков
    Future<int> asynFunc(const std::string& retval, std::chrono::nanoseconds sleepTime = 0s)
    {
        return asyncInvoke(m_aoCtx, &TestClass::funcImpl, this, retval, sleepTime);
    }

    int func(const std::string& retval, std::chrono::nanoseconds sleepTime = 0s)
    {
        return invoke(m_aoCtx, &TestClass::funcImpl, this, retval, sleepTime);
    }

    Future<void> asyncFuncWithThrow()
    {
        return asyncInvoke(m_aoCtx, &TestClass::funcWithThrowImpl, this);
    }

    void funcWithThrow()
    {
        invoke(m_aoCtx, &TestClass::funcWithThrowImpl, this);
    }

private:   // Функции выполняемые в ThreadExecuter
    int funcImpl(const std::string& arg, std::chrono::nanoseconds sleepTime)
    {
        EXPECT_EQ(m_aoCtx.executor().getThreadId(), std::this_thread::get_id());
        EXPECT_EQ(testArgValue(m_invokeCounter), arg);

        std::this_thread::sleep_for(sleepTime);

        return m_invokeCounter++;
    }

    void funcWithThrowImpl()
    {
        EXPECT_EQ(m_aoCtx.executor().getThreadId(), std::this_thread::get_id());

        ++m_invokeCounter;
        throw std::runtime_error("invokeWithThrowImpl");
    }

private:
    AOContext m_aoCtx;
    int m_invokeCounter = 0;
};

void freezeExecutor(ThreadExecutor& executor, std::chrono::nanoseconds sleepTime)
{
    executor.post([sleepTime] {
        std::this_thread::sleep_for(sleepTime);
    });
}

}   // namespace

TEST(AsyncInvoke, AsyncInvokeObject)   // NOLINT
{
    constexpr int iterCount = 10;

    ThreadExecutor executor;

    {
        TestClass object(executor);
        for (int invokeNum = 0; invokeNum < iterCount; ++invokeNum) {
            auto future = object.asynFunc(testArgValue(invokeNum));
            auto retrunedInvokeNum = future.get();
            EXPECT_EQ(retrunedInvokeNum, invokeNum);
        }
    }

    {
        TestClass object(executor);
        for (int invokeNum = 0; invokeNum < iterCount; ++invokeNum) {
            auto future = object.asyncFuncWithThrow();

            // NOLINTNEXTLINE
            EXPECT_THROW(future.get(), std::runtime_error);
        }
    }
}

TEST(AsyncInvoke, SyncInvokeObject)   // NOLINT
{
    constexpr int iterCount = 10;

    ThreadExecutor executor;

    {
        TestClass object(executor);
        for (int invokeNum = 0; invokeNum < iterCount; ++invokeNum) {
            auto retrunedInvokeNum = object.func(testArgValue(invokeNum));
            EXPECT_EQ(retrunedInvokeNum, invokeNum);
        }
    }

    {
        TestClass object(executor);
        for (int invokeNum = 0; invokeNum < iterCount; ++invokeNum) {
            EXPECT_THROW(object.funcWithThrow(), std::runtime_error);   // NOLINT
        }
    }
}

TEST(AsyncInvoke, DesctructionObjectWhenInvokeInQueue)   // NOLINT
{
    constexpr int iterCount = 10;

    ThreadExecutor executor;
    freezeExecutor(executor, 100ms);

    for (int i = 0; i < iterCount; ++i) {
        Future<int> future;

        {
            TestClass object(executor);
            future = object.asynFunc("test string");
        }

        EXPECT_THROW(future.get(), AsyncOperationWasCancelled);   // NOLINT
    }
}

TEST(AsyncInvoke, DesctructionObjectWhenInvokeActive)   // NOLINT
{
    constexpr int iterCount = 10;
    constexpr int expectedInvokeNum = 0;

    ThreadExecutor executor;
    for (int i = 0; i < iterCount; ++i) {
        Future<int> future;

        {
            TestClass object(executor);
            future = object.asynFunc(testArgValue(expectedInvokeNum), 100ms);

            // Подождем немного, чтобы метод успел вызваться
            std::this_thread::sleep_for(10ms);
        }

        int actualInvokeNum = future.get();
        EXPECT_EQ(actualInvokeNum, expectedInvokeNum);
    }
}
