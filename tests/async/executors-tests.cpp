#include <thread>
#include <memory>

#include <asio/io_context.hpp>
#include <gtest/gtest.h>

#include "nhope/async/event.h"
#include "nhope/async/io-context-executor.h"
#include "nhope/async/thread-executor.h"
#include "nhope/async/thread-pool-executor.h"

namespace {
using namespace nhope;
using namespace std::literals;

class IoContextAutoStop final
{
public:
    explicit IoContextAutoStop(std::shared_ptr<asio::io_context> ioCtx)
      : m_ioCtx(std::move(ioCtx))
    {}

    ~IoContextAutoStop()
    {
        if (m_ioCtx) {
            m_ioCtx->stop();
        }
    }

    IoContextAutoStop(IoContextAutoStop&&) = default;

    bool operator==(const asio::io_context* other) const
    {
        return m_ioCtx.get() == other;
    }

    asio::io_context& operator*()
    {
        return *m_ioCtx;
    }

private:
    std::shared_ptr<asio::io_context> m_ioCtx;
};

IoContextAutoStop startIoContext(int threadCount = 1)
{
    auto ioCtx = std::make_shared<asio::io_context>();
    for (int n = 0; n < threadCount; ++n) {
        std::thread([ioCtx] {
            auto workGuard = asio::make_work_guard(*ioCtx);
            ioCtx->run();
        }).detach();
    }

    return IoContextAutoStop(std::move(ioCtx));
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void testExec(Executor& executor, int maxParallelTasks = 1)
{
    const auto taskCount = 10 * maxParallelTasks;

    Event finished;
    std::atomic<int> finishedTasks = 0;
    std::atomic<int> activeTasks = 0;
    std::atomic<int> maxActiveTasks = 0;

    for (int i = 0; i < taskCount; ++i) {
        executor.exec([&] {
            int currentActiveTask = ++activeTasks;

            EXPECT_LE(currentActiveTask, maxParallelTasks);

            int currentMaxActiveTasks = maxActiveTasks;
            while (currentMaxActiveTasks < currentActiveTask) {
                if (maxActiveTasks.compare_exchange_strong(currentMaxActiveTasks, currentActiveTask)) {
                    break;
                }

                currentMaxActiveTasks = maxActiveTasks;
            }

            std::this_thread::sleep_for(10ms);

            --activeTasks;
            if (++finishedTasks == taskCount) {
                finished.set();
            }
        });
    }

    EXPECT_TRUE(finished.waitFor(100s));
    EXPECT_EQ(maxActiveTasks, maxParallelTasks);
}

void testExecMode(Executor& executor)
{
    {
        Event finished;
        std::atomic<bool> execEnabled = false;
        executor.exec([&] {   // Переходим в Executor
            /* Executor делжен запустить следующий Work непосредственно из следующего exec
               т.к. мы явно попросили его об этом и мы уже находимся внутри Executor-а. */
            execEnabled = true;

            executor.exec(
              [&] {
                  EXPECT_TRUE(execEnabled);
                  finished.set();
              },
              Executor::ExecMode::ImmediatelyIfPossible);

            /* Work уже должен был отработать */
            execEnabled = false;
        });

        EXPECT_TRUE(finished.waitFor(100s));
    }

    {
        Event finished;
        std::atomic<bool> execEnabled = false;
        executor.exec([&] {   // Переходим в Executor
            /* Executor не может запускать следующий Work непосредственно из следующего exec
               т.к. мы явно запретили ему это делать. */
            execEnabled = false;

            executor.exec(
              [&] {
                  EXPECT_TRUE(execEnabled);
                  finished.set();
              },
              Executor::ExecMode::AddInQueue);

            // После возврата в цикл событий должен быть выполнен запланированный Work
            execEnabled = true;
        });

        EXPECT_TRUE(finished.waitFor(100s));
    }
}

}   // namespace

TEST(ThreadExecutor, ioCtx)   // NOLINT
{
    ThreadExecutor executor;
    EXPECT_NE(&executor.ioCtx(), nullptr);
}

TEST(ThreadExecutor, Exec)   // NOLINT
{
    ThreadExecutor executor;
    EXPECT_NE(&executor.ioCtx(), nullptr);

    testExec(executor);
}

TEST(ThreadExecutor, getId)   // NOLINT
{
    ThreadExecutor executor;
    Event finished;
    executor.exec([&] {
        EXPECT_EQ(executor.id(), std::this_thread::get_id());
        finished.set();
    });

    finished.wait();
}

TEST(ThreadExecutor, ExecMode)   // NOLINT
{
    ThreadExecutor executor;
    testExecMode(executor);
}

TEST(IOContextSequenceExecutor, ioCtx)   // NOLINT
{
    auto ioCtx = startIoContext();
    IOContextSequenceExecutor executor(*ioCtx);

    EXPECT_EQ(ioCtx, &executor.ioCtx());
}

TEST(IOContextSequenceExecutor, Exec)   // NOLINT
{
    auto ioCtx = startIoContext();
    IOContextSequenceExecutor executor(*ioCtx);

    testExec(executor);
}

TEST(IOContextSequenceExecutor, ExecMode)   // NOLINT
{
    auto ioCtx = startIoContext();
    IOContextSequenceExecutor executor(*ioCtx);

    testExecMode(executor);
}

TEST(IOContextExecutor, ioCtx)   // NOLINT
{
    auto ioCtx = startIoContext(4);
    IOContextExecutor executor(*ioCtx);

    EXPECT_EQ(ioCtx, &executor.ioCtx());
}

TEST(IOContextExecutor, Exec)   // NOLINT
{
    auto ioCtx = startIoContext(2);
    IOContextExecutor executor(*ioCtx);

    testExec(executor, 2);
}

TEST(IOContextExecutor, ExecMode)   // NOLINT
{
    auto ioCtx = startIoContext(2);
    IOContextExecutor executor(*ioCtx);

    testExecMode(executor);
}

TEST(ThreadPoolExecutor, CreateDestroy)   // NOLINT
{
    ThreadPoolExecutor executor(2);
    EXPECT_NE(&executor.ioCtx(), nullptr);
    EXPECT_EQ(executor.threadCount(), 2);
}

TEST(ThreadPoolExecutor, CheckDefaultExecutor)   // NOLINT
{
    auto& executor = ThreadPoolExecutor::defaultExecutor();
    EXPECT_EQ(executor.threadCount(), std::thread::hardware_concurrency());
}

TEST(ThreadPoolExecutor, Exec)   // NOLINT
{
    ThreadPoolExecutor executor(2);
    testExec(executor, 2);
}

TEST(ThreadPoolExecutor, ExecMode)   // NOLINT
{
    ThreadPoolExecutor executor(2);

    testExecMode(executor);
}
