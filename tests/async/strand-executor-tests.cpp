#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <ostream>
#include <stdexcept>
#include <string>
#include <thread>

#include "nhope/async/io-context-executor.h"
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
        strandExecutor.exec([&, taskNum] {
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
                strandExecutor.exec([&] {
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
        strandExecutor.exec([&] {
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
            strandExecutor.exec([&] {
                ++activeTaskCount;

                EXPECT_EQ(activeTaskCount, 1);
                std::this_thread::sleep_for(1ms);

                --activeTaskCount;

                ++finishedTaskCount;
            });
        }
    }

    std::this_thread::sleep_for(200ms);
    EXPECT_LE(finishedTaskCount, taskCount);
}

TEST(StrandExecutor, FixUseAfterFreeOriginExecutor)   // NOLINT
{
    constexpr auto iterCount = 500;
    constexpr auto taskCount = 1000;
    constexpr auto executorThreadCount = 10;

    ThreadPoolExecutor executor(executorThreadCount);

    for (int iter = 0; iter < iterCount; ++iter) {
        {
            auto originExecutor = std::make_unique<IOContextExecutor>(executor.ioCtx());
            auto strandExecutor = std::make_unique<StrandExecutor>(*originExecutor);
            for (int i = 0; i < taskCount; ++i) {
                strandExecutor->exec([] {
                    std::this_thread::yield();
                });
            }
        }

        std::this_thread::yield();
    }
}
