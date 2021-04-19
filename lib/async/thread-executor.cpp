#include <cassert>
#include <utility>
#include <thread>

#include <asio/executor_work_guard.hpp>
#include <asio/post.hpp>

#include <nhope/async/thread-executor.h>

namespace nhope {

ThreadExecutor::ThreadExecutor()
  : m_ioCtx(1)
{
    m_thread = std::thread([this] {
        auto workGuard = asio::make_work_guard(m_ioCtx);
        m_ioCtx.run();
    });
}

ThreadExecutor::~ThreadExecutor()
{
    assert(std::this_thread::get_id() != this->id());   // NOLINT

    m_ioCtx.stop();
    m_thread.join();
}

ThreadExecutor::Id ThreadExecutor::id() const noexcept
{
    return m_thread.get_id();
}

void ThreadExecutor::post(Work work)
{
    asio::post(m_ioCtx, std::move(work));
}

asio::io_context& ThreadExecutor::ioCtx()
{
    return m_ioCtx;
}

}   // namespace nhope
