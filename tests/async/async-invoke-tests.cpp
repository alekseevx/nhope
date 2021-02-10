#include <chrono>
#include <cstdio>
#include <exception>
#include <stdexcept>
#include <string>
#include <thread>

#include <nhope/async/ao-context.h>
#include <nhope/async/async-invoke.h>
#include <nhope/async/thread-executor.h>

#include <gtest/gtest.h>

namespace {

using namespace nhope;
using namespace std::literals;

std::string testArgValue(int invokeNum)
{
    return "test string " + std::to_string(invokeNum);
}

class TestOneFuncExecutor
{
public:
    TestOneFuncExecutor()
      : m_thread([this] {
          std::unique_lock lock(m_mutex);
          m_cv.wait(lock, [this] {
              return m_execFunc != nullptr;
          });

          try {
              m_execFunc();
          } catch (const std::exception&) {
          }
      })
    {}

    ~TestOneFuncExecutor()
    {
        m_thread.join();
    }

    [[nodiscard]] std::thread::id getThreadId() const noexcept
    {
        return m_thread.get_id();
    }

    template<typename Fn, typename... Args>
    void post(Fn fn, Args&&... args)
    {
        m_execFunc = std::bind(fn, std::forward<Args>(args)...);
        m_cv.notify_one();
    }

private:
    std::function<void()> m_execFunc{nullptr};
    std::mutex m_mutex;
    std::condition_variable m_cv;
    std::thread m_thread;
};

// Пример потокобезопасного класса
class TestClass final
{
public:
    explicit TestClass(ThreadExecutor& executor)
      : m_aoCtx(executor)
    {}

public:   // Функции, вызываемые из внешних потоков
    Future<int> asyncFunc(const std::string& retval, std::chrono::nanoseconds sleepTime = 0s)
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
            auto future = object.asyncFunc(testArgValue(invokeNum));
            auto returnedInvokeNum = future.get();
            EXPECT_EQ(returnedInvokeNum, invokeNum);
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
            auto returnedInvokeNum = object.func(testArgValue(invokeNum));
            EXPECT_EQ(returnedInvokeNum, invokeNum);
        }
    }

    {
        TestClass object(executor);
        for (int invokeNum = 0; invokeNum < iterCount; ++invokeNum) {
            EXPECT_THROW(object.funcWithThrow(), std::runtime_error);   // NOLINT
        }
    }
}

TEST(AsyncInvoke, DestructionObjectWhenInvokeInQueue)   // NOLINT
{
    constexpr int iterCount = 10;

    ThreadExecutor executor;
    freezeExecutor(executor, 100ms);

    for (int i = 0; i < iterCount; ++i) {
        Future<int> future;

        {
            TestClass object(executor);
            future = object.asyncFunc("test string");
        }

        EXPECT_THROW(future.get(), AsyncOperationWasCancelled);   // NOLINT
    }
}

TEST(AsyncInvoke, DestructionObjectWhenInvokeActive)   // NOLINT
{
    constexpr int iterCount = 10;
    constexpr int expectedInvokeNum = 0;

    ThreadExecutor executor;
    for (int i = 0; i < iterCount; ++i) {
        Future<int> future;

        {
            TestClass object(executor);
            future = object.asyncFunc(testArgValue(expectedInvokeNum), 100ms);

            // Подождем немного, чтобы метод успел вызваться
            std::this_thread::sleep_for(10ms);
        }

        int actualInvokeNum = future.get();
        EXPECT_EQ(actualInvokeNum, expectedInvokeNum);
    }
}

TEST(AsyncInvoke, CustomAOContext)   // NOLINT
{
    TestOneFuncExecutor executor;
    BaseAOContext ctx(executor);

    int value = 4;
    constexpr auto multiply{10};

    auto f = nhope::asyncInvoke(ctx, [&] {
                 EXPECT_EQ(executor.getThreadId(), std::this_thread::get_id());
                 return value * multiply;
             }).then([](auto v) {
        return v + 2;
    });

    EXPECT_EQ(f.get(), 42);
}