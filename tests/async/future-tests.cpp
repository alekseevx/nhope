#include <exception>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>

#include <gtest/gtest.h>

#include "nhope/async/ao-context-error.h"
#include "nhope/async/detail/future-state.h"
#include "nhope/async/ao-context.h"
#include "nhope/async/future.h"
#include "nhope/async/thread-executor.h"
#include "test-helpers/wait.h"

namespace {

using namespace nhope;
using namespace std::literals;

constexpr int testValue = 10;
constexpr int invalidValue = -1;

}   // namespace

TEST(Future, makeReadyFuture)   // NOLINT
{
    {
        Future<void> future = makeReadyFuture();
        EXPECT_TRUE(future.isReady());
        EXPECT_TRUE(future.isValid());
    }

    {
        Future<void> future = makeReadyFuture<void>();
        EXPECT_TRUE(future.isReady());
        EXPECT_TRUE(future.isValid());
    }

    {
        Future<std::string> future = makeReadyFuture<std::string>("123"s);   // rvalue
        EXPECT_TRUE(future.isReady());
        EXPECT_TRUE(future.isValid());
        EXPECT_EQ(future.get(), "123");
    }

    {
        const std::string value = "123";
        Future<std::string> future = makeReadyFuture<std::string>(value);   // const ref
        EXPECT_TRUE(future.isReady());
        EXPECT_TRUE(future.isValid());
        EXPECT_EQ(future.get(), value);
    }

    {
        Future<std::string> future = makeReadyFuture<std::string>("123", 3);   // variadic
        EXPECT_TRUE(future.isReady());
        EXPECT_TRUE(future.isValid());
        EXPECT_EQ(future.get(), "123");
    }
}

TEST(Future, retrievedFlag)   // NOLINT
{
    Promise<void> p;
    EXPECT_NO_THROW(p.future());                             // NOLINT
    EXPECT_THROW(p.future(), FutureAlreadyRetrievedError);   // NOLINT

    EXPECT_NO_THROW(p.setValue());   // NOLINT
}

TEST(Future, satisfiedFlag)   // NOLINT
{
    {
        Promise<void> p;
        EXPECT_FALSE(p.satisfied());

        EXPECT_NO_THROW(p.setValue());   // NOLINT

        EXPECT_TRUE(p.satisfied());
        EXPECT_THROW(p.setValue(), PromiseAlreadySatisfiedError);   // NOLINT

        auto exPtr = std::make_exception_ptr(std::exception());
        EXPECT_THROW(p.setException(exPtr), PromiseAlreadySatisfiedError);   // NOLINT
    }

    {
        Promise<void> p;
        EXPECT_FALSE(p.satisfied());

        auto exPtr = std::make_exception_ptr(std::exception());

        EXPECT_NO_THROW(p.setException(exPtr));   // NOLINT

        EXPECT_TRUE(p.satisfied());
        EXPECT_THROW(p.setValue(), PromiseAlreadySatisfiedError);            // NOLINT
        EXPECT_THROW(p.setException(exPtr), PromiseAlreadySatisfiedError);   // NOLINT
    }
}

TEST(Future, noState)   // NOLINT
{
    {
        auto f = makeReadyFuture();
        EXPECT_TRUE(f.isValid());
        EXPECT_TRUE(f.isReady());

        Future<int> fInt;
        EXPECT_FALSE(fInt.isValid());

        EXPECT_NO_THROW(f.get());   // NOLINT

        // Now future has no state

        EXPECT_FALSE(f.isValid());
        EXPECT_THROW(f.get(), FutureNoStateError);                // NOLINT
        EXPECT_THROW(auto b = f.isReady(), FutureNoStateError);   // NOLINT
    }

    {
        auto executor = ThreadExecutor();
        auto aoCtx = AOContext(executor);
        auto f = makeReadyFuture();

        EXPECT_TRUE(f.isValid());
        EXPECT_TRUE(f.isReady());

        f.then(aoCtx, [] {});

        // Now future has no state

        EXPECT_FALSE(f.isValid());
        EXPECT_THROW(f.get(), FutureNoStateError);                      // NOLINT
        EXPECT_THROW(f.then(aoCtx, [] {}), FutureNoStateError);         // NOLINT
        EXPECT_THROW(f.then([] {}), FutureNoStateError);                // NOLINT
        EXPECT_THROW(f.fail(aoCtx, [](auto) {}), FutureNoStateError);   // NOLINT
        EXPECT_THROW(f.fail([](auto) {}), FutureNoStateError);          // NOLINT
        EXPECT_THROW(auto b = f.isReady(), FutureNoStateError);         // NOLINT
    }
}

TEST(Future, promiseResolving)   // NOLINT
{
    {
        std::list<Promise<std::string>> ls;
        auto f1 = ls.emplace_back().future();
        auto f2 = ls.emplace_back().future();

        resolvePromises(ls, "10");
        EXPECT_EQ(f1.get(), std::to_string(testValue));
        EXPECT_EQ(f2.get(), std::to_string(testValue));
        EXPECT_TRUE(ls.empty());
    }

    {
        std::list<Promise<int>> ls;
        auto f1 = ls.emplace_back().future();
        auto f2 = ls.emplace_back().future();

        resolvePromises(ls, testValue);
        EXPECT_EQ(f1.get(), testValue);
        EXPECT_EQ(f2.get(), testValue);
        EXPECT_TRUE(ls.empty());
    }

    {
        std::list<Promise<void>> ls;
        auto f1 = ls.emplace_back().future();
        auto f2 = ls.emplace_back().future();
        resolvePromises(ls);

        EXPECT_TRUE(f1.isReady());
        EXPECT_TRUE(f2.isReady());
        EXPECT_TRUE(ls.empty());
    }

    {
        std::list<Promise<int>> ls;
        auto f1 = ls.emplace_back().future();
        auto f2 = ls.emplace_back().future();
        rejectPromises(ls, std::make_exception_ptr(std::runtime_error("!!!!")));

        EXPECT_TRUE(f1.isReady());
        EXPECT_TRUE(f2.isReady());
        EXPECT_THROW(f1.get(), std::runtime_error);   // NOLINT
        EXPECT_THROW(f2.get(), std::runtime_error);   // NOLINT
        EXPECT_TRUE(ls.empty());
    }
}

TEST(Future, simpleChain)   // NOLINT
{
    auto executor = ThreadExecutor();
    auto executorThreadId = executor.id();
    auto aoCtx = AOContext(executor);

    auto future = makeReadyFuture()
                    .then(aoCtx,
                          [] {
                              return toThread<int>([] {
                                  std::this_thread::sleep_for(1s);
                                  return testValue;
                              });
                          })
                    .then(aoCtx, [executorThreadId](int value) {
                        EXPECT_EQ(executorThreadId, std::this_thread::get_id());
                        return std::to_string(value);
                    });

    EXPECT_EQ(future.get(), std::to_string(testValue));
}

TEST(Future, simpleChain2)   // NOLINT
{
    auto future = makeReadyFuture()
                    .then([] {
                        return toThread<int>([] {
                            std::this_thread::sleep_for(1s);
                            return testValue;
                        });
                    })
                    .then([](int value) {
                        return std::to_string(value);
                    });

    EXPECT_EQ(future.get(), std::to_string(testValue));
}

TEST(Future, notCaughtException)   // NOLINT
{
    ThreadExecutor executor;
    AOContext aoCtx(executor);

    auto future = makeReadyFuture()
                    .then(aoCtx,
                          []() -> int {
                              throw std::runtime_error("TestTest");
                          })
                    .then(aoCtx, [](int /*unused*/) -> int {
                        ADD_FAILURE() << "This then must not been called";
                        return 0;
                    });

    EXPECT_THROW(future.get(), std::runtime_error);   // NOLINT
}

TEST(Future, notCaughtException2)   // NOLINT
{
    auto future = makeReadyFuture()
                    .then([]() -> int {
                        throw std::runtime_error("TestTest");
                    })
                    .then([](int /*unused*/) -> int {
                        ADD_FAILURE() << "This then must not been called";
                        return 0;
                    });

    EXPECT_THROW(future.get(), std::runtime_error);   // NOLINT
}

TEST(Future, caughtException)   // NOLINT
{
    auto executor = ThreadExecutor();
    auto executorThreadId = executor.id();
    auto aoCtx = AOContext(executor);

    auto future = makeReadyFuture()
                    .then(aoCtx,
                          [] {
                              return toThread<int>([]() -> int {
                                  throw std::runtime_error("TestTest");
                              });
                          })
                    .fail(aoCtx, [executorThreadId](std::exception_ptr ex) -> int {
                        EXPECT_EQ(executorThreadId, std::this_thread::get_id());

                        // NOLINTNEXTLINE
                        EXPECT_THROW(std::rethrow_exception(ex), std::runtime_error);

                        return invalidValue;
                    });

    EXPECT_EQ(future.get(), invalidValue);
}

TEST(Future, caughtException2)   // NOLINT
{
    auto executor = ThreadExecutor();
    auto executorThreadId = executor.id();
    auto aoCtx = AOContext(executor);

    auto future = makeReadyFuture()
                    .then(aoCtx,
                          []() -> int {
                              throw std::runtime_error("TestTest");
                          })
                    .fail(aoCtx,
                          [executorThreadId](std::exception_ptr ex) -> int {
                              // NOLINTNEXTLINE
                              EXPECT_THROW(std::rethrow_exception(ex), std::runtime_error);
                              EXPECT_EQ(executorThreadId, std::this_thread::get_id());
                              return invalidValue;
                          })
                    .then(aoCtx, [](int value) {
                        EXPECT_EQ(value, invalidValue);
                        return testValue;
                    });

    EXPECT_EQ(future.get(), testValue);
}

TEST(Future, caughtException3)   // NOLINT
{
    auto future = makeReadyFuture()
                    .then([] {
                        return toThread<int>([]() -> int {
                            throw std::runtime_error("TestTest");
                        });
                    })
                    .fail([](std::exception_ptr ex) -> int {
                        // NOLINTNEXTLINE
                        EXPECT_THROW(std::rethrow_exception(ex), std::runtime_error);

                        return invalidValue;
                    });

    EXPECT_EQ(future.get(), invalidValue);
}

TEST(Future, caughtException4)   // NOLINT
{
    auto future = makeReadyFuture()
                    .then([]() -> int {
                        throw std::runtime_error("TestTest");
                    })
                    .fail([](std::exception_ptr ex) -> int {
                        // NOLINTNEXTLINE
                        EXPECT_THROW(std::rethrow_exception(ex), std::runtime_error);
                        return invalidValue;
                    })
                    .then([](int value) {
                        EXPECT_EQ(value, invalidValue);
                        return testValue;
                    });

    EXPECT_EQ(future.get(), testValue);
}

TEST(Future, skipFail)   // NOLINT
{
    auto executor = ThreadExecutor();
    auto aoCtx = AOContext(executor);

    auto future = makeReadyFuture()
                    .then(aoCtx,
                          [] {
                              return testValue;
                          })
                    .fail(aoCtx,
                          [](const std::exception_ptr& /*ex*/) -> int {
                              ADD_FAILURE() << "This then must not been called";
                              return invalidValue;
                          })
                    .then(aoCtx, [](int value) {
                        EXPECT_EQ(value, testValue);
                        return std::to_string(testValue);
                    });

    EXPECT_EQ(future.get(), std::to_string(testValue));
}

TEST(Future, skipFail2)   // NOLINT
{
    auto future = makeReadyFuture()
                    .then([] {
                        return testValue;
                    })
                    .fail([](const std::exception_ptr& /*ex*/) -> int {
                        ADD_FAILURE() << "This then must not been called";
                        return invalidValue;
                    })
                    .then([](int value) {
                        EXPECT_EQ(value, testValue);
                        return std::to_string(testValue);
                    });

    EXPECT_EQ(future.get(), std::to_string(testValue));
}

TEST(Future, dataRace)   // NOLINT
{
    static constexpr auto iterCount = 1000;

    auto executor = ThreadExecutor();
    auto aoCtx = AOContext(executor);

    for (int i = 0; i < iterCount; ++i) {
        auto promise = Promise<void>();
        auto future = promise.future();
        auto p1 = std::thread([promise = std::move(promise)]() mutable {
            promise.setValue();
        });

        auto p2 = std::thread([&aoCtx, future = std::move(future)]() mutable {
            future.then(aoCtx, [] {}).get();
        });

        p1.join();
        p2.join();
    }

    for (int i = 0; i < iterCount; ++i) {
        auto promise = Promise<void>();
        auto future = promise.future();

        auto p1 = std::thread([&aoCtx, future = std::move(future)]() mutable {
            future.then(aoCtx, [] {}).get();
        });

        auto p2 = std::thread([promise = std::move(promise)]() mutable {
            promise.setValue();
        });

        p1.join();
        p2.join();
    }
}

TEST(Future, brokenPromise)   // NOLINT
{
    Future<void> never;
    {
        Promise<void> expired;
        never = expired.future();
    }

    EXPECT_THROW(never.get(), BrokenPromiseError);   // NOLINT
}

TEST(Future, brokenPromiseAndThen)   // NOLINT
{
    auto executor = ThreadExecutor();
    auto aoCtx = AOContext(executor);

    Future<void> never;
    {
        Promise<void> expired;
        never = expired.future()
                  .then(aoCtx,
                        [] {
                            FAIL() << "never";
                        })
                  .fail(aoCtx, [](auto e) {
                      EXPECT_THROW(std::rethrow_exception(e), BrokenPromiseError);   // NOLINT
                  });
    }
    never.wait();
}

TEST(Future, makeFutureChainAfterWait)   // NOLINT
{
    auto executor = ThreadExecutor();
    auto aoCtx = AOContext(executor);

    Promise<void> p;
    auto f = p.future();

    EXPECT_FALSE(f.waitFor(0s));

    EXPECT_THROW(f.then(aoCtx, [] {}), MakeFutureChainAfterWaitError);         // NOLINT
    EXPECT_THROW(f.then([] {}), MakeFutureChainAfterWaitError);                // NOLINT
    EXPECT_THROW(f.fail(aoCtx, [](auto) {}), MakeFutureChainAfterWaitError);   // NOLINT
    EXPECT_THROW(f.fail([](auto) {}), MakeFutureChainAfterWaitError);          // NOLINT
    EXPECT_THROW(f.unwrap(), MakeFutureChainAfterWaitError);                   // NOLINT
}

TEST(Future, cancel)   // NOLINT
{
    Promise<void> p;
    auto future = p.future();

    std::thread thread([p = std::move(p)]() mutable {
        while (!p.cancelled()) {
            ;
        }
        p.setException(std::make_exception_ptr(AsyncOperationWasCancelled()));
    });

    EXPECT_FALSE(future.waitFor(100ms));

    future.cancel();

    EXPECT_THROW(future.get(), AsyncOperationWasCancelled);   //NOLINT
    thread.join();
}

TEST(Future, cancelThen)   // NOLINT
{
    Promise<void> p;

    auto future = p.future();

    std::thread thread([p = std::move(p)]() mutable {
        while (!p.cancelled()) {
            ;
        }
        p.setValue();
    });

    nhope::ThreadExecutor th;
    nhope::AOContext ao(th);

    auto future2 = future.then(ao, [] {
        FAIL() << "must be cancelled";
    });

    EXPECT_FALSE(future2.waitFor(100ms));
    future2.cancel();

    EXPECT_THROW(future2.get(), AsyncOperationWasCancelled);   //NOLINT

    thread.join();
}

TEST(Future, cancelFail)   // NOLINT
{
    Promise<void> p;

    auto future = p.future();

    std::thread thread([p = std::move(p)]() mutable {
        while (!p.cancelled()) {
            ;
        }
        p.setException(std::make_exception_ptr(AsyncOperationWasCancelled()));
    });

    nhope::ThreadExecutor th;
    nhope::AOContext ao(th);

    auto future2 = future.fail(ao, [](auto ex) {
        EXPECT_THROW(std::rethrow_exception(ex), AsyncOperationWasCancelled);   // NOLINT
    });

    EXPECT_FALSE(future2.waitFor(100ms));
    future2.cancel();

    EXPECT_NO_THROW(future2.get());   //NOLINT

    thread.join();
}

TEST(Future, cancelThenWithoutAOContext)   // NOLINT
{
    Promise<void> p;

    auto future = p.future();

    std::thread thread([p = std::move(p)]() mutable {
        while (!p.cancelled()) {
            ;
        }
        p.setException(std::make_exception_ptr(AsyncOperationWasCancelled()));
    });

    auto future2 = future.then([] {
        FAIL() << "must be cancelled";
    });

    EXPECT_FALSE(future2.waitFor(100ms));
    future2.cancel();

    EXPECT_THROW(future2.get(), AsyncOperationWasCancelled);   //NOLINT

    thread.join();
}

TEST(Future, cancelUnwrap)   // NOLINT
{
    auto f = toThread<Future<Future<void>>>([] {
                 return toThread<Future<void>>([] {
                     Promise<void> p;
                     auto innerFuture = p.future();
                     std::thread([p = std::move(p)]() mutable {
                         while (!p.cancelled()) {
                             ;
                         }
                         p.setException(std::make_exception_ptr(AsyncOperationWasCancelled()));
                     }).detach();

                     return innerFuture;
                 });
             }).unwrap();

    f.cancel();

    EXPECT_THROW(f.get(), AsyncOperationWasCancelled);   //NOLINT
}

TEST(Future, cancelChain)   // NOLINT
{
    nhope::ThreadExecutor executor;
    nhope::AOContext ao(executor);

    auto f = makeReadyFuture()
               .then(ao,
                     [] {
                         std::this_thread::sleep_for(200ms);
                     })
               .then(ao,
                     [] {
                         FAIL() << "chain was cancelled";
                     })
               .fail(ao, [](auto e) {
                   EXPECT_THROW(std::rethrow_exception(e), AsyncOperationWasCancelled);   // NOLINT
                   throw std::runtime_error("test");
               });
    f.cancel();

    EXPECT_THROW(f.get(), std::runtime_error);   // NOLINT
}

TEST(Future, parallelCancel)   // NOLINT
{
    constexpr auto iterCount = 1000;

    for (auto i = 0; i < iterCount; ++i) {
        Promise<void> p;
        auto f = p.future();

        auto th1 = std::thread([&f] {
            f.cancel();
        });
        auto th2 = std::thread([&f] {
            f.cancel();
        });

        EXPECT_TRUE(waitForPred(1s, [&p] {
            return p.cancelled();
        }));

        th1.join();
        th2.join();
    }
}
