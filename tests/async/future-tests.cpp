#include <stdexcept>
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

TEST(Future, simpleChain)   // NOLINT
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
    auto future = makeReadyFuture()
                    .then([]() -> int {
                        throw std::runtime_error("TestTest");
                    })
                    .then([](int /*unused*/) -> int {
                        ADD_FAILURE() << "This thenValue must not been called";
                        return 0;
                    });

    EXPECT_THROW(int v = future.get(), std::runtime_error);   // NOLINT
}

TEST(Future, caughtException)   // NOLINT
{
    auto future = makeReadyFuture()
                    .then([] {
                        return toThread<int>([]() -> int {
                            throw std::runtime_error("TestTest");
                        });
                    })
                    .fail([](std::exception_ptr ex) -> int {
                        EXPECT_THROW(std::rethrow_exception(ex), std::runtime_error);   // NOLINT
                        return invalidValue;
                    });

    EXPECT_EQ(future.get(), invalidValue);
}

TEST(Future, caughtException2)   // NOLINT
{
    auto future = makeReadyFuture()
                    .then([]() -> int {
                        throw std::runtime_error("TestTest");
                    })
                    .fail([](std::exception_ptr ex) -> int {
                        EXPECT_THROW(std::rethrow_exception(ex), std::runtime_error);   // NOLINT
                        return invalidValue;
                    })
                    .then([](int value) {
                        EXPECT_EQ(value, invalidValue);
                        return testValue;
                    });

    EXPECT_EQ(future.get(), testValue);
}

TEST(Future, skipThenException)   // NOLINT
{
    auto future = makeReadyFuture()
                    .then([] {
                        return testValue;
                    })
                    .fail([](const std::exception_ptr& /*ex*/) -> int {
                        ADD_FAILURE() << "This thenValue must not been called";
                        return invalidValue;
                    })
                    .then([](int value) {
                        EXPECT_EQ(value, testValue);
                        return std::to_string(testValue);
                    });

    EXPECT_EQ(future.get(), std::to_string(testValue));
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

    EXPECT_EQ(future5.state(), FutureState::ready);
    EXPECT_EQ(future6.state(), FutureState::ready);
    EXPECT_TRUE(lvoids.empty());
}

TEST(Future, simpleChainWithAOCtx)   // NOLINT
{
    auto executor = ThreadExecutor();
    auto executorThreadId = executor.getThreadId();
    auto aoCtx = AOContext(executor);

    auto future = makeReadyFuture()
                    .then([] {
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

TEST(Future, notCaughtExceptionWithAOCtx)   // NOLINT
{
    ThreadExecutor executor;
    AOContext aoCtx(executor);

    auto future = makeReadyFuture()
                    .then([]() -> int {
                        throw std::runtime_error("TestTest");
                    })
                    .then(aoCtx, [](int /*unused*/) -> int {
                        ADD_FAILURE() << "This thenValue must not been called";
                        return 0;
                    });

    EXPECT_THROW(future.get(), std::runtime_error);   // NOLINT
}

TEST(Future, caughtExceptionWithAOCtx)   // NOLINT
{
    auto executor = ThreadExecutor();
    auto executorThreadId = executor.getThreadId();
    auto aoCtx = AOContext(executor);

    auto future = makeReadyFuture()
                    .then([] {
                        return toThread<int>([]() -> int {
                            throw std::runtime_error("TestTest");
                        });
                    })
                    .fail(aoCtx, [executorThreadId](std::exception_ptr ex) -> int {
                        EXPECT_EQ(executorThreadId, std::this_thread::get_id());
                        EXPECT_THROW(std::rethrow_exception(ex), std::runtime_error);   // NOLINT

                        return invalidValue;
                    });

    EXPECT_EQ(future.get(), invalidValue);
}

TEST(Future, caughtException2WithAOCtx)   // NOLINT
{
    auto executor = ThreadExecutor();
    auto executorThreadId = executor.getThreadId();
    auto aoCtx = AOContext(executor);

    auto future = makeReadyFuture()
                    .then([]() -> int {
                        throw std::runtime_error("TestTest");
                    })
                    .fail(aoCtx,
                          [executorThreadId](std::exception_ptr ex) -> int {
                              EXPECT_THROW(std::rethrow_exception(ex), std::runtime_error);   // NOLINT
                              EXPECT_EQ(executorThreadId, std::this_thread::get_id());

                              return invalidValue;
                          })
                    .then([](int value) {
                        EXPECT_EQ(value, invalidValue);
                        return testValue;
                    });

    EXPECT_EQ(future.get(), testValue);
}

TEST(Future, skipThenExceptionWithAOCtx)   // NOLINT
{
    auto executor = ThreadExecutor();
    auto executorThreadId = executor.getThreadId();
    auto aoCtx = AOContext(executor);

    auto future = makeReadyFuture()
                    .then([] {
                        return testValue;
                    })
                    .fail(aoCtx,
                          [](const std::exception_ptr& /*ex*/) -> int {
                              ADD_FAILURE() << "This thenValue must not been called";
                              return invalidValue;
                          })
                    .then([](int value) {
                        EXPECT_EQ(value, testValue);
                        return std::to_string(testValue);
                    });

    EXPECT_EQ(future.get(), std::to_string(testValue));
}
