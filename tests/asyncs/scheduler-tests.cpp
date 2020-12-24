#include <thread>
#include <functional>
#include <gtest/gtest.h>

#include <nhope/asyncs/async-invoke.h>
#include <nhope/asyncs/ao-context.h>
#include <nhope/asyncs/manageable-task.h>
#include <nhope/asyncs/scheduler.h>
#include <nhope/asyncs/thread-executor.h>

using namespace std::literals;
using namespace nhope::asyncs;

class ThreadStub
{
public:
    ThreadStub(Scheduler& s)
      : m_ao(m_th)
      , m_sched(s)
    {}

    void push(ManageableTask::TaskFunction&& f)
    {
        invoke(m_ao, [this, func = std::move(f)]() mutable {
            m_sched.push(std::move(func));
        });
    }

    void cancel(Scheduler::TaskId id)
    {
        invoke(m_ao, [this, id] {
            m_sched.cancel(id);
        });
    }

    void clear()
    {
        invoke(m_ao, [this] {
            m_sched.clear();
        });
    }

private:
    ThreadExecutor m_th;
    AOContext m_ao;
    Scheduler& m_sched;
};

TEST(Scheduler, SimpleTaskChain)   // NOLINT
{
    std::atomic_int counter = 0;
    Scheduler scheduler;

    auto f1 = [&counter](auto& /*unused*/) {
        std::this_thread::sleep_for(300ms);
        EXPECT_EQ(counter, 0);
        counter = 1;
    };

    auto f2 = [&counter](auto& /*unused*/) {
        std::this_thread::sleep_for(200ms);
        EXPECT_EQ(counter, 1);
        counter = 2;
    };

    auto f3 = [&counter](auto& /*unused*/) {
        EXPECT_EQ(counter, 2);
        std::this_thread::sleep_for(100ms);
        counter = 3;
    };

    EXPECT_EQ(scheduler.push(f1), 0);
    EXPECT_EQ(scheduler.getActiveTaskId(), 0);
    EXPECT_EQ(scheduler.push(f2), 1);
    EXPECT_EQ(scheduler.push(f3), 2);

    scheduler.waitAll();
    EXPECT_EQ(scheduler.getActiveTaskId(), std::nullopt);

    EXPECT_EQ(counter, 3);
}

TEST(Scheduler, PriorityTask)   // NOLINT
{
    thread_local int localCounter{0};
    constexpr auto threadCounter{100};
    std::atomic_int counter = 0;
    Scheduler scheduler;

    auto f1 = [&counter](auto& ctx) {
        while (ctx.checkPoint()) {
            std::this_thread::sleep_for(2ms);
            if (localCounter++ == threadCounter) {
                break;
            }
        }
        EXPECT_EQ(counter, 2);
        counter = 1;
    };

    auto f2 = [&counter](auto& ctx) {
        while (ctx.checkPoint()) {
            std::this_thread::sleep_for(2ms);
            if (localCounter++ == threadCounter) {
                break;
            }
        }
        EXPECT_EQ(counter, 3);
        counter = 2;
    };

    auto f3 = [&counter](auto& ctx) {
        while (ctx.checkPoint()) {
            std::this_thread::sleep_for(2ms);
            if (localCounter++ == threadCounter) {
                break;
            }
        }
        counter = 3;
    };

    EXPECT_EQ(scheduler.push(f1, 0), 0);
    EXPECT_EQ(scheduler.push(f2, 1), 1);
    EXPECT_EQ(scheduler.push(f3, 2), 2);

    scheduler.waitAll();
    EXPECT_EQ(scheduler.getActiveTaskId(), std::nullopt);

    EXPECT_EQ(counter, 1);
}

TEST(Scheduler, CancelTask)   // NOLINT
{
    thread_local int localCounter{0};
    constexpr auto threadCounter{100};
    std::atomic_int counter = 0;
    Scheduler scheduler;

    auto f1 = [&counter](auto& ctx) {
        while (ctx.checkPoint()) {
            std::this_thread::sleep_for(2ms);
            if (localCounter++ == threadCounter) {
                break;
            }
        }
        counter = 1;
    };

    auto f2 = [&counter](auto& ctx) {
        while (ctx.checkPoint()) {
            std::this_thread::sleep_for(1100ms);
            if (localCounter++ == threadCounter) {
                ADD_FAILURE() << "fail";
                break;
            }
        }
        EXPECT_EQ(counter, 1);
        counter = 2;
    };

    auto f3 = [&counter](auto& ctx) {
        while (ctx.checkPoint()) {
            std::this_thread::sleep_for(2ms);
            if (localCounter++ == threadCounter) {
                break;
            }
        }
        EXPECT_EQ(counter, 1);
        counter = 3;
    };

    EXPECT_EQ(scheduler.push(f1), 0);
    EXPECT_EQ(scheduler.push(f2), 1);
    EXPECT_EQ(scheduler.push(f3), 2);

    scheduler.cancel(1);

    scheduler.waitAll();
    EXPECT_EQ(scheduler.getActiveTaskId(), std::nullopt);

    EXPECT_EQ(counter, 3);
}

TEST(Scheduler, ClearTask)   // NOLINT
{
    thread_local int localCounter{0};
    constexpr auto threadCounter{100};
    std::atomic_int counter = 0;
    Scheduler scheduler;

    auto f1 = [&counter](auto& ctx) {
        while (ctx.checkPoint()) {
            std::this_thread::sleep_for(1ms);
            if (localCounter++ == threadCounter) {
                EXPECT_EQ(counter, 0);
                counter = 1;
                break;
            }
        }
    };

    auto f2 = [&counter](auto& ctx) {
        while (ctx.checkPoint()) {
            std::this_thread::sleep_for(1ms);
            if (localCounter++ == threadCounter) {
                EXPECT_EQ(counter, 1);
                counter = 2;
                break;
            }
        }
    };

    auto f3 = [&counter](auto& ctx) {
        while (ctx.checkPoint()) {
            std::this_thread::sleep_for(1ms);
            if (localCounter++ == threadCounter) {
                EXPECT_EQ(counter, 2);
                counter = 3;
                break;
            }
        }
    };

    EXPECT_EQ(scheduler.push(f1, 0), 0);
    std::this_thread::sleep_for(10ms);
    EXPECT_EQ(scheduler.getActiveTaskId(), 0);
    EXPECT_EQ(scheduler.push(f2, 1), 1);
    std::this_thread::sleep_for(10ms);
    EXPECT_EQ(scheduler.push(f3, 2), 2);

    std::this_thread::sleep_for(100ms);

    scheduler.clear();
    scheduler.waitAll();

    EXPECT_EQ(counter, 0);
}

TEST(Scheduler, TaskWait)   // NOLINT
{
    thread_local int localCounter{0};
    constexpr auto threadCounter{100};
    std::atomic_int counter = 0;
    Scheduler scheduler;
    auto f1 = [&counter](auto& /*unused*/) {
        std::this_thread::sleep_for(300ms);
        EXPECT_EQ(counter, 0);
        counter = 1;
    };

    auto f2 = [&counter](auto& /*unused*/) {
        std::this_thread::sleep_for(200ms);
        EXPECT_EQ(counter, 1);
        counter = 2;
    };

    auto f3 = [&counter](auto& /*unused*/) {
        EXPECT_EQ(counter, 2);
        std::this_thread::sleep_for(100ms);
        counter = 3;
    };

    EXPECT_EQ(scheduler.push(f1), 0);
    EXPECT_EQ(scheduler.push(f2), 1);

    EXPECT_EQ(scheduler.getActiveTaskId(), 0);
    scheduler.wait(1);

    EXPECT_EQ(scheduler.getActiveTaskId(), std::nullopt);

    EXPECT_EQ(scheduler.push(f3), 2);

    scheduler.wait(2);
    EXPECT_EQ(scheduler.getActiveTaskId(), std::nullopt);
}

TEST(Scheduler, ThreadRace)   // NOLINT
{
    thread_local int localCounter{0};
    constexpr auto threadCounter{100};
    std::atomic_int counter = 0;
    Scheduler scheduler;

    auto f1 = [&counter](auto& ctx) {
        while (ctx.checkPoint()) {
            std::this_thread::sleep_for(10ms);
            if (localCounter++ == threadCounter) {
                break;
            }
            counter += 1;
        }
    };

    ThreadStub racer1(scheduler);
    ThreadStub racer2(scheduler);

    racer1.push(f1);
    racer2.push(f1);
    racer1.push(f1);

    racer1.cancel(1);
    ASSERT_EQ(counter, 100);

    scheduler.waitAll();
    EXPECT_EQ(scheduler.getActiveTaskId(), std::nullopt);
    EXPECT_EQ(counter, 200);
}

TEST(Scheduler, CancellingByDestruction)   // NOLINT
{
    thread_local int localCounter{0};
    constexpr auto threadCounter{100};
    std::atomic_int counter = 0;

    auto f1 = [&counter](auto& ctx) {
        while (ctx.checkPoint()) {
            std::this_thread::sleep_for(10ms);
            if (localCounter++ == threadCounter) {
                FAIL() << "failed";
                break;
            }
        }
    };

    {
        Scheduler scheduler;
        scheduler.push(f1);
        scheduler.push(f1);
        scheduler.push(f1);
        std::this_thread::sleep_for(10ms);
    }
}