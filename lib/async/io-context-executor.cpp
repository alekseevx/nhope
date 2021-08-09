#include <utility>
#include <asio/dispatch.hpp>
#include <asio/io_context.hpp>
#include <asio/post.hpp>

#include <nhope/async/io-context-executor.h>

namespace nhope {

IOContextExecutor::IOContextExecutor(asio::io_context& ioCtx)
  : m_ioCtx(ioCtx)
{}

void IOContextExecutor::exec(Work work, ExecMode mode)
{
    if (mode == ExecMode::AddInQueue) {
        asio::post(m_ioCtx, std::move(work));
    } else {
        asio::dispatch(m_ioCtx, std::move(work));
    }
}

asio::io_context& IOContextExecutor::ioCtx()
{
    return m_ioCtx;
}

IOContextSequenceExecutor::IOContextSequenceExecutor(asio::io_context& ioCtx)
  : m_ioCtx(ioCtx)
{}

void IOContextSequenceExecutor::exec(Work work, ExecMode mode)
{
    if (mode == ExecMode::AddInQueue) {
        asio::post(m_ioCtx, std::move(work));
    } else {
        asio::dispatch(m_ioCtx, std::move(work));
    }
}

asio::io_context& IOContextSequenceExecutor::ioCtx()
{
    return m_ioCtx;
}

}   // namespace nhope
