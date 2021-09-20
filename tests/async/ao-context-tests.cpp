#include <atomic>
#include <chrono>
#include <functional>
#include <list>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>

#include "nhope/async/ao-context-close-handler.h"
#include "nhope/async/ao-context-error.h"
#include "nhope/async/ao-context.h"
#include "nhope/async/async-invoke.h"
#include "nhope/async/event.h"
#include "nhope/async/thread-executor.h"
#include "nhope/async/thread-pool-executor.h"

#include <fmt/format.h>
#include <gtest/gtest.h>

#include "nhope/async/timer.h"
#include "test-helpers/wait.h"

namespace {

using namespace nhope;
using namespace std::literals;

class TestAOContextCloseHandler final : public AOContextCloseHandler
{
public:
    TestAOContextCloseHandler() = default;

    explicit TestAOContextCloseHandler(std::function<void()> handler)
      : m_handler(std::move(handler))
    {}

    ~TestAOContextCloseHandler() = default;

    void aoContextClose() noexcept override
    {
        if (m_handler) {
            m_handler();
        }
    }

    void setHandler(std::function<void()> handler)
    {
        m_handler = std::move(handler);
    }

private:
    std::function<void()> m_handler;
};

template<typename Fn>
std::list<std::thread> startParallel(int threadCount, Fn&& fn)
{
    std::list<std::thread> threads;
    for (int i = 0; i < threadCount; ++i) {
        threads.emplace_back(std::forward<Fn>(fn));
    }
    return threads;
}

void waitForFinished(std::list<std::thread>& threads)
{
    while (!threads.empty()) {
        threads.front().join();
        threads.pop_front();
    }
}

}   // namespace

TEST(AOContext, SequentialExecution)   // NOLINT
{
    constexpr auto iterCount = 100;

    constexpr auto executorThreadCount = 10;
    ThreadPoolExecutor executor(executorThreadCount);

    AOContext aoContext(executor);

    std::atomic<int> activeHandlerCount = 0;
    std::atomic<int> finishedHandlerCount = 0;

    for (int operationNum = 0; operationNum < iterCount; ++operationNum) {
        aoContext.exec([&, operationNum] {
            EXPECT_EQ(++activeHandlerCount, 1);
            std::this_thread::sleep_for(1ms);
            --activeHandlerCount;

            EXPECT_EQ(finishedHandlerCount, operationNum);
            ++finishedHandlerCount;
        });
    }

    EXPECT_TRUE(waitForValue(100 * 1ms * iterCount, finishedHandlerCount, iterCount));
}

TEST(AOContext, ExceptionInExec)   // NOLINT
{
    constexpr auto iterCount = 100;

    ThreadExecutor executor;
    AOContext aoContext(executor);

    std::atomic<int> asyncOperationHandlerCalled = 0;
    for (int i = 0; i < iterCount; ++i) {
        aoContext.exec([&] {
            asyncOperationHandlerCalled++;
            throw std::runtime_error("TestException");
        });
    }

    EXPECT_TRUE(waitForValue(1s, asyncOperationHandlerCalled, iterCount));
}

TEST(AOContext, ExplicitClose)   // NOLINT
{
    ThreadExecutor executor;
    AOContext aoContext(executor);
    aoContext.close();

    auto callFlag = std::make_shared<std::atomic<bool>>(false);

    aoContext.exec([callFlag] {
        *callFlag = true;
    });

    // Wait until lambda was is destroyed
    EXPECT_TRUE(waitForPred(100s, [&callFlag] {
        return callFlag.use_count() == 1;
    }));

    // Check than lambda was not called
    EXPECT_TRUE(*callFlag == false);
}

TEST(AOContext, ExplicitCloseFromExec)   // NOLINT
{
    constexpr auto iterCount = 100;

    ThreadExecutor executor;

    for (int i = 0; i < iterCount; ++i) {
        AOContext aoContext(executor);

        aoContext.exec([&aoContext] {
            aoContext.close();
        });

        std::this_thread::yield();

        auto callFlag = std::make_shared<std::atomic<bool>>(false);
        aoContext.exec([callFlag] {
            callFlag->store(true);
        });

        // Wait until lambda was is destroyed
        EXPECT_TRUE(waitForPred(100s, [&callFlag] {
            return callFlag.use_count() == 1;
        }));

        // Check than lambda was not called
        EXPECT_TRUE(*callFlag == false);
    }
}

TEST(AOContext, MakeChildAfterClose)   // NOLINT
{
    AOContext parent(ThreadPoolExecutor::defaultExecutor());
    parent.close();

    EXPECT_THROW(AOContext child(parent), AOContextClosed);   // NOLINT
}

TEST(AOContext, ParallelClose)   // NOLINT
{
    static constexpr auto threadCount = 10;
    static constexpr auto iterCount = 100;

    ThreadExecutor executor;

    for (auto i = 0; i < iterCount; ++i) {
        AOContext aoCtx(executor);

        Event closeEvent;
        auto threads = startParallel(threadCount, [&] {
            closeEvent.wait();
            aoCtx.close();
        });

        aoCtx.exec([&] {
            closeEvent.wait();
            aoCtx.close();
        });

        std::this_thread::sleep_for(10ms);
        closeEvent.set();

        waitForFinished(threads);
    }
}

TEST(AOContext, CloseParent)   // NOLINT
{
    AOContext parent(ThreadPoolExecutor::defaultExecutor());

    AOContext child(parent);
    EXPECT_EQ(&parent.executor(), &child.executor());

    parent.close();
    EXPECT_FALSE(child.isOpen());
}

TEST(AOContext, CloseParentFromChild)   // NOLINT
{
    AOContext parent(ThreadPoolExecutor::defaultExecutor());

    Event parentClosed;
    AOContext child(parent);
    child.exec([&] {
        parent.close();
        EXPECT_FALSE(parent.isOpen());
        EXPECT_FALSE(child.isOpen());

        parentClosed.set();
    });

    EXPECT_TRUE(parentClosed.waitFor(10s));
}

TEST(AOContext, WorkWithParentAndChildren)   // NOLINT
{
    constexpr auto executorThreadCount = 10;
    ThreadPoolExecutor executor(executorThreadCount);

    std::atomic<int> activeWorkCount = 0;

    // NOLINTNEXTLINE(readability-function-cognitive-complexity)
    const auto workFn = [&activeWorkCount](AOContextRef aoCtx) {
        while (aoCtx.isOpen()) {
            aoCtx.exec([&] {
                EXPECT_EQ(activeWorkCount.fetch_add(1), 0);

                std::this_thread::yield();

                EXPECT_EQ(activeWorkCount.fetch_sub(1), 1);
            });

            std::this_thread::yield();
        }
    };

    AOContext parent(executor);
    std::thread(workFn, AOContextRef(parent)).detach();

    AOContext firstChild(parent);
    std::thread(workFn, AOContextRef(firstChild)).detach();

    AOContext secondChild(parent);
    std::thread(workFn, AOContextRef(secondChild)).detach();

    std::this_thread::sleep_for(5s);

    parent.close();
    EXPECT_FALSE(firstChild.isOpen());
    EXPECT_FALSE(secondChild.isOpen());
    EXPECT_FALSE(waitForPred(100ms, [&activeWorkCount] {
        return activeWorkCount != 0;
    }));
}

TEST(AOContext, AddCloseHandler)   // NOLINT
{
    ThreadExecutor executor;
    AOContext aoCtx(executor);

    bool closHandlerCalled = false;
    TestAOContextCloseHandler closeHandler([&] {
        closHandlerCalled = true;
    });
    aoCtx.addCloseHandler(closeHandler);
    aoCtx.close();

    EXPECT_TRUE(closHandlerCalled);
}

TEST(AOContext, RemoveCloseHandler)   // NOLINT
{
    ThreadExecutor executor;
    AOContext aoCtx(executor);

    bool closHandlerCalled = false;
    TestAOContextCloseHandler closeHandler([&closHandlerCalled] {
        closHandlerCalled = true;
    });
    aoCtx.addCloseHandler(closeHandler);
    aoCtx.removeCloseHandler(closeHandler);
    aoCtx.close();

    EXPECT_FALSE(closHandlerCalled);
}

TEST(AOContext, RemoveCloseHandlerFromCloseHandler)   // NOLINT
{
    ThreadExecutor executor;
    AOContext aoCtx(executor);

    bool closHandlerCalled = false;
    TestAOContextCloseHandler closeHandler;
    closeHandler.setHandler([&] {
        aoCtx.removeCloseHandler(closeHandler);
        closHandlerCalled = true;
    });
    aoCtx.addCloseHandler(closeHandler);
    aoCtx.close();

    EXPECT_TRUE(closHandlerCalled);
}

TEST(AOContext, RemoveCloseHandlerWhenCloseHandlerIsCalled)   // NOLINT
{
    static constexpr auto iterCount = 1000;

    ThreadExecutor executor;

    for (auto i = 0; i < iterCount; ++i) {
        AOContext aoCtx(executor);

        Event closeHandlerIsCalled;
        Event closeHandlerIsRemoved;

        TestAOContextCloseHandler closeHandler([&] {
            closeHandlerIsCalled.set();
            std::this_thread::yield();
        });
        aoCtx.addCloseHandler(closeHandler);

        std::thread([&] {
            closeHandlerIsCalled.wait();
            aoCtx.removeCloseHandler(closeHandler);
            closeHandlerIsRemoved.set();
        }).detach();

        std::this_thread::yield();

        aoCtx.close();
        EXPECT_TRUE(closeHandlerIsRemoved.waitFor(10s));
    }
}

TEST(AOContext, ParallelAddRemoveCloseHandler)   // NOLINT
{
    static constexpr auto threadCount = 10;
    static constexpr auto time = 5s;

    ThreadExecutor executor;
    AOContext aoCtx(executor);
    std::atomic<bool> closeFlag = false;

    auto threads = startParallel(threadCount, [&] {
        for (;;) {
            try {
                TestAOContextCloseHandler closeHandler;
                aoCtx.addCloseHandler(closeHandler);
                std::this_thread::yield();
                aoCtx.removeCloseHandler(closeHandler);
            } catch (const AOContextClosed&) {
                break;
            }
        }
    });

    std::this_thread::sleep_for(time);
    aoCtx.close();

    waitForFinished(threads);
}

TEST(AOContext, AOContextRef)   // NOLINT
{
    ThreadExecutor executor;
    AOContext aoCtx(executor);
    AOContextRef aoCtxRef(aoCtx);

    EXPECT_TRUE(aoCtxRef.isOpen());

    Event wasExecuted;

    EXPECT_FALSE(aoCtxRef.workInThisThread());
    aoCtxRef.exec([&] {
        EXPECT_TRUE(aoCtxRef.workInThisThread());
        wasExecuted.set();
    });

    EXPECT_TRUE(wasExecuted.waitFor(10s));
}

TEST(AOContext, ConcurentCloseChildAndParent)   // NOLINT
{
    constexpr auto itercount{1000};
    ThreadExecutor executor;
    for (size_t i = 0; i < itercount; i++) {
        AOContext aoCtx(executor);
        auto childAoCtx = std::make_unique<AOContext>(aoCtx);
        childAoCtx->exec([&] {
            childAoCtx.reset();
        });
        aoCtx.close();
    }
}
