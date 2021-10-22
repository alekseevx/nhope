#include <cassert>
#include <memory>
#include <string>
#include <utility>
#include <thread>

#include <asio/dispatch.hpp>
#include <asio/executor_work_guard.hpp>
#include <asio/io_context.hpp>
#include <asio/post.hpp>

#include "nhope/async/detail/thread-name.h"
#include "nhope/async/thread-executor.h"

namespace nhope {

using WorkGuard = asio::executor_work_guard<asio::io_context::executor_type>;

struct ThreadExecutor::Impl final
{
    explicit Impl(const std::string& name)
      : ioCtx(1)
      , workGuard(ioCtx.get_executor())
      , thread([this, name] {
          detail::setThreadName(name);
          ioCtx.run();
      })
    {}

    ~Impl()
    {
        assert(std::this_thread::get_id() != thread.get_id());   // NOLINT
        this->stop();
    }

    void stop()
    {
        ioCtx.stop();
        thread.join();
    }

    asio::io_context ioCtx;
    WorkGuard workGuard;
    std::thread thread;
};

ThreadExecutor::ThreadExecutor(const std::string& name)
  : m_d(name)
{}

ThreadExecutor::~ThreadExecutor() = default;

ThreadExecutor::Id ThreadExecutor::id() const noexcept
{
    return m_d->thread.get_id();
}

void ThreadExecutor::exec(Work work, ExecMode mode)
{
    if (mode == ExecMode::AddInQueue) {
        asio::post(m_d->ioCtx, std::move(work));
    } else {
        asio::dispatch(m_d->ioCtx, std::move(work));
    }
}

asio::io_context& ThreadExecutor::ioCtx()
{
    return m_d->ioCtx;
}

}   // namespace nhope
