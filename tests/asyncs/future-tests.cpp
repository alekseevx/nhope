#include <stdexcept>
#include <string>
#include <thread>

#include <gtest/gtest.h>

#include <nhope/asyncs/future.h>

namespace {

using namespace nhope::asyncs;
using namespace std::literals;

constexpr int testValue = 10;
constexpr int invalidValue = -1;

}   // namespace

TEST(Future, simpleChain)   // NOLINT
{
    auto future = makeReadyFuture()
                    .thenValue([] {
                        return toThread<int>([] {
                            std::this_thread::sleep_for(1s);
                            return testValue;
                        });
                    })
                    .thenValue([](int value) {
                        return std::to_string(value);
                    });

    EXPECT_EQ(future.get(), std::to_string(testValue));
}

TEST(Future, notCaughtException)   // NOLINT
{
    auto future = makeReadyFuture()
                    .thenValue([]() -> int {
                        throw std::runtime_error("TestTest");
                    })
                    .thenValue([](int /*unused*/) -> int {
                        ADD_FAILURE() << "This thenValue must not been called";
                        return 0;
                    });

    EXPECT_THROW(int v = future.get(), std::runtime_error);   // NOLINT
}

TEST(Future, caughtException)   // NOLINT
{
    auto future = makeReadyFuture()
                    .thenValue([] {
                        return toThread<int>([]() -> int {
                            throw std::runtime_error("TestTest");
                        });
                    })
                    .thenException([](std::exception_ptr ex) -> int {
                        EXPECT_THROW(std::rethrow_exception(ex), std::runtime_error);   // NOLINT
                        return invalidValue;
                    });

    EXPECT_EQ(future.get(), invalidValue);
}

TEST(Future, caughtException2)   // NOLINT
{
    auto future = makeReadyFuture()
                    .thenValue([]() -> int {
                        throw std::runtime_error("TestTest");
                    })
                    .thenException([](std::exception_ptr ex) -> int {
                        EXPECT_THROW(std::rethrow_exception(ex), std::runtime_error);   // NOLINT
                        return invalidValue;
                    })
                    .thenValue([](int value) {
                        EXPECT_EQ(value, invalidValue);
                        return testValue;
                    });

    EXPECT_EQ(future.get(), testValue);
}

TEST(Future, skipThenException)   // NOLINT
{
    auto future = makeReadyFuture()
                    .thenValue([] {
                        return testValue;
                    })
                    .thenException([](const std::exception_ptr& /*ex*/) -> int {
                        ADD_FAILURE() << "This thenValue must not been called";
                        return invalidValue;
                    })
                    .thenValue([](int value) {
                        EXPECT_EQ(value, testValue);
                        return std::to_string(testValue);
                    });

    EXPECT_EQ(future.get(), std::to_string(testValue));
}
