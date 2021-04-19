#include <utility>
#include <asio/io_context.hpp>

#include <nhope/async/io-context-executor.h>

namespace nhope {

IOContextExecutor::IOContextExecutor(asio::io_context& ioCtx)
  : m_ioCtx(ioCtx)
{}

void IOContextExecutor::post(Work work)
{
    m_ioCtx.post(std::move(work));
}

asio::io_context& IOContextExecutor::ioCtx()
{
    return m_ioCtx;
}

IOContextSequenceExecutor::IOContextSequenceExecutor(asio::io_context& ioCtx)
  : m_ioCtx(ioCtx)
{}

void IOContextSequenceExecutor::post(Work work)
{
    m_ioCtx.post(std::move(work));
}

asio::io_context& IOContextSequenceExecutor::ioCtx()
{
    return m_ioCtx;
}

}   // namespace nhope
