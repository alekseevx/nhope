#include <cassert>
#include <list>
#include <memory>
#include <mutex>

#include "nhope/async/reverse-lock.h"
#include "nhope/async/strand-executor.h"

namespace nhope {

class StrandExecutor::Impl final : public std::enable_shared_from_this<Impl>
{
public:
    explicit Impl(Executor& executor)
      : m_originExecutor(executor)
    {}

    void exec(Work work, ExecMode /*mode*/)
    {
        std::unique_lock lock(m_mutex);
        m_taskQueue.emplace_back(std::move(work));

        if (m_idle) {
            m_idle = false;
            this->scheduleNextWork(lock);
        }
    }

    void clear()
    {
        std::scoped_lock lock(m_mutex);
        m_taskQueue.clear();
    }

    Executor& originExecutor() const noexcept
    {
        return m_originExecutor;
    }

private:
    void scheduleNextWork([[maybe_unused]] std::unique_lock<std::mutex>& lock)
    {
        assert(lock.owns_lock());   // NOLINT

        if (m_taskQueue.empty()) {
            m_idle = true;
            return;
        }

        m_originExecutor.exec([self = shared_from_this()] {
            std::unique_lock lock(self->m_mutex);

            assert(!self->m_idle);   // NOLINT

            if (self->m_taskQueue.empty()) {
                self->m_idle = true;
                return;
            }

            Work nextWork = std::move(self->m_taskQueue.front());
            self->m_taskQueue.pop_front();

            try {
                ReverseLock unlock(lock);
                nextWork();
            } catch (...) {
                // FIXME: Logging
            }

            self->scheduleNextWork(lock);
        });
    }

    Executor& m_originExecutor;

    std::mutex m_mutex;
    std::list<Work> m_taskQueue;

    bool m_idle = true;
};

StrandExecutor::StrandExecutor(Executor& executor)
  : m_d(std::make_shared<Impl>(executor))
{}

StrandExecutor::~StrandExecutor()
{
    m_d->clear();
}

Executor& StrandExecutor::originExecutor() noexcept
{
    return m_d->originExecutor();
}

void StrandExecutor::exec(Work work, ExecMode mode)
{
    m_d->exec(std::move(work), mode);
}

asio::io_context& StrandExecutor::ioCtx()
{
    return this->originExecutor().ioCtx();
}

}   // namespace nhope
