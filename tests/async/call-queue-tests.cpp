#include <atomic>
#include <stdexcept>
#include <thread>

#include <gtest/gtest.h>

#include "nhope/async/thread-executor.h"
#include "nhope/async/thread-pool-executor.h"
#include "nhope/async/call-queue.h"

namespace {

using namespace nhope;
using namespace std::chrono_literals;

Future<int> doWork(int value)
{
    static std::atomic_int counter{};
    return toThread([value] {
        EXPECT_EQ(counter, 0);
        ++counter;
        std::this_thread::sleep_for(10ms);
        --counter;
        return value + 4;
    });
}

}   // namespace

TEST(CallQueue, Queue)   // NOLINT
{
    auto& executor = ThreadPoolExecutor::defaultExecutor();
    AOContext ctx(executor);

    CallQueue calls;

    constexpr auto taskCount = 100;
    Future<int> f;
    for (int taskNum = 0; taskNum < taskCount; ++taskNum) {
        f = calls.push(ctx, doWork, taskNum);
    }
    EXPECT_EQ(f.get(), taskCount + 3);

    auto simple = [] {
        return 1;
    };

    auto simpleVoid = [] {
        std::this_thread::sleep_for(10ms);
    };

    EXPECT_EQ(calls.push(ctx, simple).get(), 1);
    calls.push(ctx, simpleVoid).get();
}

TEST(CallQueue, Exceptions)   // NOLINT
{
    auto& executor = ThreadPoolExecutor::defaultExecutor();
    AOContext ctx(executor);

    CallQueue calls;

    auto exceptVoid = [] {
        throw std::runtime_error("error");
    };

    auto except = [](int val) {
        if (val == 1) {
            throw std::runtime_error("error");
        }
        return val;
    };

    EXPECT_THROW(calls.push(ctx, except, 1).get(), std::runtime_error);    //NOLINT
    EXPECT_THROW(calls.push(ctx, exceptVoid).get(), std::runtime_error);   //NOLINT
}
