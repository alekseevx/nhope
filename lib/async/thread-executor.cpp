#include <cassert>
#include <thread>

#include <asio/executor_work_guard.hpp>

#include "nhope/async/thread-executor.h"

using namespace nhope;

ThreadExecutor::ThreadExecutor()
{
    m_thread = std::thread([this] {
        auto workGuard = asio::make_work_guard(m_ioCtx);
        m_ioCtx.run();
    });
}

ThreadExecutor::~ThreadExecutor()
{
    assert(std::this_thread::get_id() != this->getThreadId());   // NOLINT

    m_ioCtx.stop();
    m_thread.join();
}

ThreadExecutor::Id ThreadExecutor::getThreadId() const noexcept
{
    return m_thread.get_id();
}

asio::io_context& ThreadExecutor::getContext() noexcept
{
    return m_ioCtx;
}
