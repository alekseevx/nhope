#include <boost/asio/post.hpp>
#include <boost/asio/strand.hpp>
#include <chrono>
#include <exception>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <string>
#include <thread>

#include <boost/asio/executor_work_guard.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/chrono/duration.hpp>
#include <boost/thread/futures/future_status.hpp>

#include <nhope/asyncs/func-executor.h>

#include <gtest/gtest.h>
#include <utility>

namespace {
using namespace nhope::asyncs;
using namespace std::literals;

constexpr auto CallTimeout = boost::chrono::seconds(10);

class Executor final
{
public:
    Executor()
    {
        m_thread = std::thread([this] {
            auto workGuard = boost::asio::make_work_guard(m_ctx);
            m_ctx.run();
        });
    }

    ~Executor()
    {
        m_ctx.stop();
        m_thread.join();
    }

    template<typename Fn>
    void post(Fn&& fn)
    {
        boost::asio::post(m_ctx, std::forward<Fn>(fn));
    }

private:
    boost::asio::io_context m_ctx;
    std::thread m_thread;
};

}   // namespace

TEST(FuncExecutor, AsyncCallWithoutResult)   // NOLINT
{
    Executor executor;
    FuncExecutor<Executor> funcExecutor(executor);

    std::atomic<bool> wasCall = false;
    auto future = funcExecutor.asyncCall([&wasCall] {
        wasCall = true;
    });

    EXPECT_EQ(future.wait_for(CallTimeout), boost::future_status::ready);
    EXPECT_TRUE(wasCall);
}

TEST(FuncExecutor, AsyncCallWithExcept)   // NOLINT
{
    Executor executor;
    FuncExecutor<Executor> funcExecutor(executor);

    auto future = funcExecutor.asyncCall([] {
        throw std::runtime_error("Except");
    });

    EXPECT_EQ(future.wait_for(CallTimeout), boost::future_status::ready);
    EXPECT_THROW(future.get(), std::runtime_error);   // NOLINT
}

TEST(FuncExecutor, AsyncCallWithResult)   // NOLINT
{
    constexpr int ExpectedResult = 15555;

    Executor executor;
    FuncExecutor<Executor> funcExecutor(executor);

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

    Executor executor;
    FuncExecutor<Executor> funcExecutor(executor);

    auto func = [Arg1, Arg2](std::string_view arg1, int arg2) {
        EXPECT_EQ(arg1, Arg1);
        EXPECT_EQ(arg2, Arg2);
    };
    auto future = funcExecutor.asyncCall(func, Arg1, Arg2);

    EXPECT_EQ(future.wait_for(CallTimeout), boost::future_status::ready);
}

TEST(FuncExecutor, CallWithoutResult)   // NOLINT
{
    Executor executor;
    FuncExecutor<Executor> funcExecutor(executor);

    std::atomic<bool> wasCall = false;
    funcExecutor.call([&wasCall] {
        wasCall = true;
    });

    EXPECT_TRUE(wasCall);
}

TEST(FuncExecutor, CallWithExcept)   // NOLINT
{
    Executor executor;
    FuncExecutor<Executor> funcExecutor(executor);

    // clang-format off
    EXPECT_THROW(funcExecutor.call([] {   // NOLINT
        throw std::runtime_error("Except");
    }), std::runtime_error);
    // clang-format on
}

TEST(FuncExecutor, CallWithResult)   // NOLINT
{
    constexpr int ExpectedResult = 15555;

    Executor executor;
    FuncExecutor<Executor> funcExecutor(executor);

    auto actualResult = funcExecutor.call([] {
        return ExpectedResult;
    });

    EXPECT_EQ(actualResult, ExpectedResult);
}

TEST(FuncExecutor, CallWithArgs)   // NOLINT
{
    const auto Arg1 = "Arg1"s;
    const auto Arg2 = 77777;

    Executor executor;
    FuncExecutor<Executor> funcExecutor(executor);

    auto func = [Arg1, Arg2](std::string_view arg1, int arg2) {
        EXPECT_EQ(arg1, Arg1);
        EXPECT_EQ(arg2, Arg2);
    };

    funcExecutor.call(func, Arg1, Arg2);
}
