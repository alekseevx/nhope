#pragma once

#include <boost/asio/io_context.hpp>
#include <nhope/asyncs/func-executor.h>

#include <boost/asio.hpp>
#include <type_traits>

namespace nhope::asyncs {
class ThreadExecutor final : private boost::asio::noncopyable
{
public:
    ThreadExecutor()
      : m_thread([this] {
          auto workGuard = boost::asio::make_work_guard(m_ioCtx);
          m_ioCtx.run();
      })
      , m_executor(m_ioCtx)
    {}

    ~ThreadExecutor()
    {
        m_ioCtx.stop();
        if (m_thread.joinable()) {
            m_thread.join();
        }
    }

    [[nodiscard]] boost::asio::io_context& getContext() noexcept
    {
        return m_ioCtx;
    }

    FuncExecutor<boost::asio::io_context> getExecutor() noexcept
    {
        return m_executor;
    }

    template<typename Fn, typename... Args>
    void post(Fn fn, Args&&... args)
    {
        m_executor.asyncCall(std::forward<Fn>(fn), std::forward<Args>(args)...);
    }

    template<typename Fn, typename... Args>
    std::invoke_result_t<Fn, Args...> call(Fn fn, Args&&... args)
    {
        return m_executor.call(std::forward<Fn>(fn), std::forward<Args>(args)...);
    }

private:
    boost::asio::io_context m_ioCtx;
    std::thread m_thread;
    FuncExecutor<boost::asio::io_context> m_executor;
};

}   // namespace nhope::asyncs