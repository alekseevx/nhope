#include <thread>
#include <functional>
#include <gtest/gtest.h>
// #include <iostream>

#include <nhope/async/async-invoke.h>
#include <nhope/async/ao-context.h>
#include <nhope/async/manageable-task.h>
#include <nhope/async/scheduler.h>
#include <nhope/async/thread-executor.h>

using namespace std::literals;
using namespace nhope;

class ThreadStub
{
public:
    ThreadStub(Scheduler& s)
      : m_ao(m_th)
      , m_sched(s)
    {}

    Scheduler::TaskId push(ManageableTask::TaskFunction&& f)
    {
        return invoke(m_ao, [this, func = std::move(f)]() mutable {
            return m_sched.push(std::move(func));
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
    static constexpr auto threadCounter{100};
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
    static constexpr auto threadCounter{100};
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
    static constexpr auto threadCounter{100};
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
    static constexpr auto threadCounter{100};
    std::atomic_int counter = 0;
    Scheduler scheduler;

    auto f1 = [&counter](auto& ctx) {
        while (ctx.checkPoint()) {
            std::this_thread::sleep_for(5ms);
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
    auto c3 = racer1.push(f1);
    EXPECT_EQ(scheduler.size(), 3);
    racer1.cancel(c3);

    scheduler.waitAll();
    EXPECT_EQ(scheduler.getActiveTaskId(), std::nullopt);
    EXPECT_EQ(counter, 200);
}

TEST(Scheduler, CancellingByDestruction)   // NOLINT
{
    thread_local int localCounter{0};
    static constexpr auto threadCounter{100};
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

TEST(Scheduler, DeactivateTask)   // NOLINT
{
    thread_local int localCounter{0};
    static constexpr auto threadCounter{100};
    std::atomic_int counter = 0;

    auto f1 = [&counter](auto& ctx) {
        // std::cout << "start f1" << std::endl;
        ctx.setBeforePause([] {
            // std::cout << "pausing f1" << std::endl;
            return true;
        });
        ctx.setAfterPause([] {
            // std::cout << "resuming f1" << std::endl;
        });

        while (ctx.checkPoint()) {
            std::this_thread::sleep_for(10ms);
            if (localCounter++ == threadCounter) {
                break;
            }
        }
        // std::cout << "finish f1" << std::endl;
    };

    auto f2 = [&counter](auto& ctx) {
        // std::cout << "start f2" << std::endl;
        ctx.setBeforePause([] {
            // std::cout << "pausing f2" << std::endl;
            return true;
        });
        ctx.setAfterPause([] {
            // std::cout << "resuming f2" << std::endl;
        });

        while (ctx.checkPoint()) {
            std::this_thread::sleep_for(10ms);
            if (localCounter++ == threadCounter) {
                break;
            }
        }
        // std::cout << "finish f2" << std::endl;
    };

    auto f3 = [&counter](auto& ctx) {
        // std::cout << "start f3" << std::endl;
        ctx.setBeforePause([] {
            // std::cout << "pausing f3" << std::endl;
            return true;
        });
        ctx.setAfterPause([] {
            // std::cout << "resuming f3" << std::endl;
        });

        while (ctx.checkPoint()) {
            std::this_thread::sleep_for(10ms);
            if (localCounter++ == threadCounter) {
                break;
            }
        }
        // std::cout << "finish f3" << std::endl;
    };

    Scheduler scheduler;
    scheduler.push(f1, 1);
    scheduler.push(f2);
    scheduler.push(f3);

    std::this_thread::sleep_for(50ms);
    scheduler.deactivate(0);
    std::this_thread::sleep_for(50ms);
    scheduler.activate(0);
    std::this_thread::sleep_for(10ms);
    scheduler.deactivate(1);
    scheduler.wait(0);
    std::this_thread::sleep_for(10ms);
    scheduler.deactivate(2);

    scheduler.activate(1);
    scheduler.wait(1);
    std::this_thread::sleep_for(100ms);
    scheduler.activate(2);

    scheduler.waitAll();
}

TEST(Scheduler, WaitDeactivatedTask)   // NOLINT
{
    thread_local int localCounter{0};
    static constexpr auto threadCounter{100};
    std::atomic_int counter = 0;

    auto f1 = [&counter](auto& ctx) {
        while (ctx.checkPoint()) {
            std::this_thread::sleep_for(10ms);
            if (localCounter++ == threadCounter) {
                break;
            }
        }
    };

    Scheduler scheduler;
    scheduler.push(f1);

    std::this_thread::sleep_for(10ms);
    scheduler.deactivate(0);
    EXPECT_EQ(scheduler.getActiveTaskId(), std::nullopt);
    auto f = scheduler.asyncWait(0);
    std::this_thread::sleep_for(100ms);
    ASSERT_FALSE(f.isReady());
    scheduler.activate(0);
    f.get();
    ASSERT_FALSE(f.valid());
}

TEST(Scheduler, CancelDeactivatedTask)   // NOLINT
{
    thread_local int localCounter{0};
    static constexpr auto threadCounter{100};
    std::atomic_int counter = 0;

    auto f1 = [&counter](auto& ctx) {
        while (ctx.checkPoint()) {
            std::this_thread::sleep_for(10ms);
            if (localCounter++ == threadCounter) {
                FAIL() << "was not cancelled";
                break;
            }
        }
    };

    Scheduler scheduler;
    scheduler.push(f1);

    std::this_thread::sleep_for(10ms);
    scheduler.deactivate(0);
    EXPECT_EQ(scheduler.getActiveTaskId(), std::nullopt);
    scheduler.cancel(0);
}

TEST(Scheduler, DeactivateByRequest)   // NOLINT
{
    thread_local int localCounter{0};
    static constexpr auto threadCounter{100};

    auto work = [counter = 0](auto& ctx) {
        while (ctx.checkPoint()) {
            std::this_thread::sleep_for(10ms);
            if (localCounter++ == threadCounter) {
                break;
            }
        }
    };

    nhope::Future<void> deactivated;
    {
        Scheduler scheduler;
        auto firstWorkId = scheduler.push(work);

        std::this_thread::sleep_for(10ms);
        scheduler.deactivate(firstWorkId);
        EXPECT_EQ(scheduler.getActiveTaskId(), std::nullopt);
        deactivated = scheduler.asyncWait(firstWorkId);
        std::this_thread::sleep_for(100ms);
        ASSERT_FALSE(deactivated.isReady());

        auto secondWorkId = scheduler.push(work);
        auto activeWorkWaiter = scheduler.asyncWait(secondWorkId);

        activeWorkWaiter.get();
        ASSERT_FALSE(deactivated.waitFor(200ms));
    }

    ASSERT_TRUE(deactivated.valid());
    deactivated.get();
    ASSERT_FALSE(deactivated.valid());
}

TEST(Scheduler, CancelNotStarted)   // NOLINT
{
    Scheduler scheduler;
    constexpr auto iterCount{10};
    auto f1 = [counter = 0](auto& /*unused*/) mutable {
        while (counter++ < iterCount) {
            std::this_thread::sleep_for(100ms);
        }
    };

    auto f2 = [](auto& /*unused*/) {
        FAIL() << "started cancelled";
    };

    EXPECT_EQ(scheduler.push(f1), 0);
    auto cancelId = scheduler.push(f2);
    EXPECT_EQ(scheduler.size(), 2);
    scheduler.cancel(cancelId);
    EXPECT_EQ(scheduler.size(), 1);

    scheduler.waitAll();
    EXPECT_EQ(scheduler.getActiveTaskId(), std::nullopt);
}
