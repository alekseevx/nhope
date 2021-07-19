#include <exception>
#include <string>
#include <thread>

#include <gtest/gtest.h>

#include <nhope/async/ao-context.h>
#include <nhope/async/future.h>
#include <nhope/async/thread-executor.h>

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

        EXPECT_NO_THROW(p.setValue());                              // NOLINT
        EXPECT_THROW(p.setValue(), PromiseAlreadySatisfiedError);   // NOLINT

        auto exPtr = std::make_exception_ptr(std::exception());
        EXPECT_THROW(p.setException(exPtr), PromiseAlreadySatisfiedError);   // NOLINT
    }

    {
        Promise<void> p;

        auto exPtr = std::make_exception_ptr(std::exception());

        EXPECT_NO_THROW(p.setException(exPtr));   // NOLINT

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
        EXPECT_THROW(f.fail(aoCtx, [](auto) {}), FutureNoStateError);   // NOLINT
        EXPECT_THROW(auto b = f.isReady(), FutureNoStateError);         // NOLINT
    }
}

TEST(Future, promiseResolving)   // NOLINT
{
    std::list<Promise<std::string>> l;
    auto future1 = l.emplace_back().future();
    auto future2 = l.emplace_back().future();

    resolvePromises(l, "10");
    EXPECT_EQ(future1.get(), std::to_string(testValue));
    EXPECT_EQ(future2.get(), std::to_string(testValue));
    EXPECT_TRUE(l.empty());

    std::list<Promise<int>> lints;
    auto future3 = lints.emplace_back().future();
    auto future4 = lints.emplace_back().future();

    resolvePromises(lints, testValue);
    EXPECT_EQ(future3.get(), testValue);
    EXPECT_EQ(future4.get(), testValue);
    EXPECT_TRUE(lints.empty());

    std::list<Promise<void>> lvoids;
    auto future5 = lvoids.emplace_back().future();
    auto future6 = lvoids.emplace_back().future();
    resolvePromises(lvoids);

    EXPECT_TRUE(future5.isReady());
    EXPECT_TRUE(future6.isReady());
    EXPECT_TRUE(lvoids.empty());
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

TEST(Future, skipThenExceptionWithAOCtx)   // NOLINT
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
    auto executor = ThreadExecutor();
    auto aoCtx = AOContext(executor);

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
    EXPECT_THROW(f.fail(aoCtx, [](auto) {}), MakeFutureChainAfterWaitError);   // NOLINT
    EXPECT_THROW(f.unwrap(), MakeFutureChainAfterWaitError);                   // NOLINT
}
