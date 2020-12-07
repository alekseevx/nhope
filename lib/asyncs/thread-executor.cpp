#include <cassert>
#include <thread>

#include <boost/asio/executor_work_guard.hpp>

#include "nhope/asyncs/thread-executor.h"

using namespace nhope::asyncs;

ThreadExecutor::ThreadExecutor()
{
    m_thread = std::thread([this] {
        auto workGuard = boost::asio::make_work_guard(m_ioCtx);
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

boost::asio::io_context& ThreadExecutor::getContext() noexcept
{
    return m_ioCtx;
}
