#include "nhope/async/ao-context.h"

namespace nhope {

using namespace detail;

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

void AOContext::addCloseHandler(AOContextCloseHandler& closeHandler)
{
    m_aoImpl->addCloseHandler(&closeHandler);
}

void AOContext::removeCloseHandler(AOContextCloseHandler& closeHandler) noexcept
{
    m_aoImpl->removeCloseHandler(&closeHandler);
}

[[nodiscard]] bool AOContext::workInThisThread() const noexcept
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

bool AOContextRef::isOpen() const noexcept
{
    return m_aoImpl->isOpen();
}

void AOContextRef::addCloseHandler(AOContextCloseHandler& closeHandler)
{
    m_aoImpl->addCloseHandler(&closeHandler);
}

void AOContextRef::removeCloseHandler(AOContextCloseHandler& closeHandler) noexcept
{
    m_aoImpl->removeCloseHandler(&closeHandler);
}

[[nodiscard]] bool AOContextRef::workInThisThread() const noexcept
{
    return m_aoImpl->aoContextWorkInThisThread();
}

}   // namespace nhope
