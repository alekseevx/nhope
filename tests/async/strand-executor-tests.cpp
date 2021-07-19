#include <atomic>
#include <chrono>
#include <mutex>
#include <stdexcept>
#include <thread>

#include "nhope/async/strand-executor.h"
#include "nhope/async/thread-executor.h"
#include "nhope/async/thread-pool-executor.h"

#include <gtest/gtest.h>

#include "test-helpers/wait.h"

using namespace nhope;
using namespace std::literals;

TEST(StrandExecutor, SequentialExecution)   // NOLINT
{
    constexpr auto taskCount = 100;
    constexpr auto executorThreadCount = 10;
    ThreadPoolExecutor executor(executorThreadCount);

    StrandExecutor strandExecutor(executor);

    EXPECT_EQ(&strandExecutor.originExecutor(), &executor);
    EXPECT_EQ(&strandExecutor.ioCtx(), &executor.ioCtx());

    std::atomic<int> activeTaskCount = 0;
    std::atomic<int> finishedTaskCount = 0;

    const auto startTime = std::chrono::steady_clock::now();
    for (int taskNum = 0; taskNum < taskCount; ++taskNum) {
        strandExecutor.post([&, taskNum] {
            ++activeTaskCount;

            EXPECT_EQ(activeTaskCount, 1);
            std::this_thread::sleep_for(1ms);

            --activeTaskCount;

            EXPECT_EQ(finishedTaskCount, taskNum);
            ++finishedTaskCount;
        });
    }

    EXPECT_TRUE(waitForValue(100 * 1ms * taskCount, finishedTaskCount, taskCount));
    const auto stopTime = std::chrono::steady_clock::now();

    EXPECT_GE(stopTime - startTime, 1ms * taskCount);
}

TEST(StrandExecutor, ThreadSafe)   // NOLINT
{
    constexpr auto executorThreadCount = 10;
    ThreadPoolExecutor executor(executorThreadCount);
    StrandExecutor strandExecutor(executor);

    std::atomic<int> activeTaskCount = 0;
    std::atomic<int> finishedTaskCount = 0;

    constexpr auto taskCountPerThread = 10000;
    constexpr auto threadCount = 4;
    constexpr auto taskCount = taskCountPerThread * threadCount;

    for (auto threadNum = 0; threadNum < threadCount; ++threadNum) {
        std::thread([&] {
            for (int i = 0; i < taskCountPerThread; ++i) {
                strandExecutor.post([&] {
                    ++activeTaskCount;
                    EXPECT_EQ(activeTaskCount, 1);
                    --activeTaskCount;

                    ++finishedTaskCount;
                });
            }
        }).detach();
    }

    EXPECT_TRUE(waitForValue(5s, finishedTaskCount, taskCount));
}

TEST(StrandExecutor, ExceptionInWork)   // NOLINT
{
    constexpr auto taskCount = 10;
    constexpr auto executorThreadCount = 10;
    ThreadPoolExecutor executor(executorThreadCount);

    StrandExecutor strandExecutor(executor);

    std::atomic<int> finishedTaskCount = 0;

    for (int i = 0; i < taskCount; ++i) {
        strandExecutor.post([&] {
            ++finishedTaskCount;
            throw std::runtime_error("TestException");
        });
    }

    EXPECT_TRUE(waitForValue(100ms, finishedTaskCount, taskCount));
}

TEST(StrandExecutor, Destroy)   // NOLINT
{
    constexpr auto taskCount = 10;
    constexpr auto executorThreadCount = 10;
    ThreadPoolExecutor executor(executorThreadCount);

    std::atomic<int> activeTaskCount = 0;
    std::atomic<int> finishedTaskCount = 0;
    {
        StrandExecutor strandExecutor(executor);

        for (int i = 0; i < taskCount; ++i) {
            strandExecutor.post([&] {
                ++activeTaskCount;

                EXPECT_EQ(activeTaskCount, 1);
                std::this_thread::sleep_for(1ms);

                --activeTaskCount;

                ++finishedTaskCount;
            });
        }
    }

    // Tasks should run even after the StrandExecutor is destroyed
    EXPECT_TRUE(waitForValue(100 * 1ms * taskCount, finishedTaskCount, taskCount));
}

TEST(StrandExecutor, ReuseImpl)   // NOLINT
{
    constexpr auto executorThreadCount = 10;
    ThreadPoolExecutor executor(executorThreadCount);

    StrandExecutor strandExecutor(executor);
    StrandExecutor strandExecutor2(static_cast<Executor&>(strandExecutor));
    EXPECT_EQ(&strandExecutor.originExecutor(), &strandExecutor2.originExecutor());
}

TEST(StrandExecutor, UseSequenceExecutor)   // NOLINT
{
    constexpr auto taskCount = 10;
    ThreadExecutor seqExecutor;
    StrandExecutor strandExecutor(seqExecutor);

    std::atomic<int> finishedTaskCount = 0;
    for (int i = 0; i < taskCount; ++i) {
        strandExecutor.post([&] {
            ++finishedTaskCount;
        });
    }

    EXPECT_TRUE(waitForValue(100 * 1ms * taskCount, finishedTaskCount, taskCount));
}
