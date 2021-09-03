#include "nhope/async/ao-context.h"
#include "nhope/async/ao-handler.h"

namespace nhope {

using namespace detail;

AOHandlerCall::AOHandlerCall(AOHandlerId id, RefPtr<detail::AOContextImpl> aoImpl)
  : m_id(id)
  , m_aoImpl(std::move(aoImpl))
{}

void AOHandlerCall::operator()(Executor::ExecMode mode)
{
    m_aoImpl->callAOHandler(m_id, mode);
}

AOContext::AOContext(Executor& executor)
  : m_aoImpl(AOContextImpl::makeRoot(executor))
{}

AOContext::AOContext(AOContext& parent)
  : m_aoImpl(parent.m_aoImpl->makeChild())
{}

AOContext::~AOContext()
{
    m_aoImpl->close();
}

bool AOContext::isOpen() const noexcept
{
    return m_aoImpl->isOpen();
}

void AOContext::close()
{
    m_aoImpl->close();
}

AOHandlerCall AOContext::putAOHandler(std::unique_ptr<AOHandler> handler)
{
    const auto id = m_aoImpl->putAOHandler(std::move(handler));
    return AOHandlerCall(id, m_aoImpl);
}

void AOContext::callAOHandler(std::unique_ptr<AOHandler> handler, Executor::ExecMode mode)
{
    const auto id = m_aoImpl->putAOHandler(std::move(handler));
    m_aoImpl->callAOHandler(id, mode);
}

[[nodiscard]] bool AOContext::workInThisThread() const
{
    return m_aoImpl->aoContextWorkInThisThread();
}

SequenceExecutor& AOContext::executor()
{
    return m_aoImpl->executor();
}

AOContextRef::AOContextRef(AOContext& aoCtx) noexcept
  : m_aoImpl(aoCtx.m_aoImpl)
{}

AOHandlerCall AOContextRef::putAOHandler(std::unique_ptr<AOHandler> handler)
{
    const auto id = m_aoImpl->putAOHandler(std::move(handler));
    return AOHandlerCall(id, m_aoImpl);
}

void AOContextRef::callAOHandler(std::unique_ptr<AOHandler> handler, Executor::ExecMode mode)
{
    const auto id = m_aoImpl->putAOHandler(std::move(handler));
    m_aoImpl->callAOHandler(id, mode);
}

}   // namespace nhope
