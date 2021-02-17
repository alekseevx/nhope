#pragma once

#include <thread>
#include <utility>

#include <asio/io_context.hpp>
#include <asio/post.hpp>
#include <boost/noncopyable.hpp>

namespace nhope {

class ThreadExecutor final : boost::noncopyable
{
public:
    using Id = std::thread::id;

public:
    ThreadExecutor();
    ~ThreadExecutor();

    [[nodiscard]] Id getThreadId() const noexcept;

    [[nodiscard]] asio::io_context& getContext() noexcept;

    template<typename Fn, typename... Args>
    void post(Fn fn, Args&&... args)
    {
        asio::post(m_ioCtx, std::forward<Fn>(fn), std::forward<Args>(args)...);
    }

private:
    asio::io_context m_ioCtx;
    std::thread m_thread;
};

}   // namespace nhope
