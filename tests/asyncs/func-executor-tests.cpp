#include <stdexcept>
#include <string_view>
#include <string>
#include <thread>
#include <utility>

#include <boost/asio.hpp>
#include <boost/chrono.hpp>
#include <boost/thread.hpp>

#include <nhope/asyncs/func-executor.h>
#include <nhope/asyncs/thread-executor.h>

#include <gtest/gtest.h>

namespace {
using namespace nhope::asyncs;
using namespace std::literals;

constexpr auto CallTimeout = boost::chrono::seconds(10);

}   // namespace

TEST(FuncExecutor, AsyncCallWithoutResult)   // NOLINT
{
    ThreadExecutor executor;
    FuncExecutor<ThreadExecutor> funcExecutor(executor);

    std::atomic<bool> wasCall = false;
    auto future = funcExecutor.asyncCall([&wasCall] {
        wasCall = true;
    });

    EXPECT_EQ(future.wait_for(CallTimeout), boost::future_status::ready);
    EXPECT_TRUE(wasCall);
}

TEST(FuncExecutor, AsyncCallWithExcept)   // NOLINT
{
    ThreadExecutor executor;
    FuncExecutor<ThreadExecutor> funcExecutor(executor);

    auto future = funcExecutor.asyncCall([] {
        throw std::runtime_error("Except");
    });

    EXPECT_EQ(future.wait_for(CallTimeout), boost::future_status::ready);
    EXPECT_THROW(future.get(), std::runtime_error);   // NOLINT
}

TEST(FuncExecutor, AsyncCallWithResult)   // NOLINT
{
    constexpr int ExpectedResult = 15555;

    ThreadExecutor executor;
    FuncExecutor<ThreadExecutor> funcExecutor(executor);

    auto future = funcExecutor.asyncCall([] {
        return ExpectedResult;
    });

    EXPECT_EQ(future.wait_for(CallTimeout), boost::future_status::ready);

    const auto actualResult = future.get();
    EXPECT_EQ(actualResult, ExpectedResult);
}

TEST(FuncExecutor, AsyncCallWithArgs)   // NOLINT
{
    const auto Arg1 = "Arg1"s;
    const auto Arg2 = 77777;

    ThreadExecutor executor;
    FuncExecutor<ThreadExecutor> funcExecutor(executor);

    auto func = [Arg1, Arg2](std::string_view arg1, int arg2) {
        EXPECT_EQ(arg1, Arg1);
        EXPECT_EQ(arg2, Arg2);
    };
    auto future = funcExecutor.asyncCall(func, Arg1, Arg2);

    EXPECT_EQ(future.wait_for(CallTimeout), boost::future_status::ready);
}

TEST(FuncExecutor, CallWithoutResult)   // NOLINT
{
    ThreadExecutor executor;
    FuncExecutor<ThreadExecutor> funcExecutor(executor);

    std::atomic<bool> wasCall = false;
    funcExecutor.call([&wasCall] {
        wasCall = true;
    });

    EXPECT_TRUE(wasCall);
}

TEST(FuncExecutor, CallWithExcept)   // NOLINT
{
    ThreadExecutor executor;
    FuncExecutor<ThreadExecutor> funcExecutor(executor);

    // clang-format off
    EXPECT_THROW(funcExecutor.call([] {   // NOLINT
        throw std::runtime_error("Except");
    }), std::runtime_error);
    // clang-format on
}

TEST(FuncExecutor, CallWithResult)   // NOLINT
{
    constexpr int ExpectedResult = 15555;

    ThreadExecutor executor;
    FuncExecutor<ThreadExecutor> funcExecutor(executor);

    auto actualResult = funcExecutor.call([] {
        return ExpectedResult;
    });

    EXPECT_EQ(actualResult, ExpectedResult);
}

TEST(FuncExecutor, CallWithArgs)   // NOLINT
{
    const auto Arg1 = "Arg1"s;
    const auto Arg2 = 77777;

    ThreadExecutor executor;
    FuncExecutor<ThreadExecutor> funcExecutor(executor);

    auto func = [Arg1, Arg2](std::string_view arg1, int arg2) {
        EXPECT_EQ(arg1, Arg1);
        EXPECT_EQ(arg2, Arg2);
    };

    funcExecutor.call(func, Arg1, Arg2);
}
