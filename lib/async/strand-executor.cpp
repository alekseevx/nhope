#include <cassert>
#include <list>
#include <memory>
#include <mutex>
#include <thread>

#include <nhope/async/strand-executor.h>

namespace nhope {

bool isSequenceExecutor(Executor& executor)
{
    return dynamic_cast<SequenceExecutor*>(&executor) != nullptr;
}

class StrandExecutor::Impl final : public std::enable_shared_from_this<Impl>
{
public:
    explicit Impl(Executor& executor)
      : originExecutor(executor)
      , isOriginSeqExecutor(isSequenceExecutor(executor))
    {}

    void exec(Work& work, ExecMode mode)
    {
        if (isOriginSeqExecutor) {
            originExecutor.exec(std::move(work), mode);
            return;
        }

        std::scoped_lock lock(mutex);

        if (hasActiveWork) {
            waitingQueue.emplace_back(std::move(work));
            return;
        }

        this->scheduleWork(work);
    }

    void scheduleWork(Work& work)
    {
        assert(!hasActiveWork);   // NOLINT

        hasActiveWork = true;
        originExecutor.exec([self = shared_from_this(), work = std::move(work)] {
            try {
                work();
            } catch (...) {
                // FIXME: Logging
            }
            self->workFinished();
        });
    }

    void workFinished()
    {
        std::scoped_lock lock(mutex);
        hasActiveWork = false;

        if (waitingQueue.empty()) {
            return;
        }

        this->scheduleWork(waitingQueue.front());
        waitingQueue.pop_front();
    }

    Executor& originExecutor;
    const bool isOriginSeqExecutor;

    std::mutex mutex;
    std::list<Work> waitingQueue;

    bool hasActiveWork = false;
};

StrandExecutor::StrandExecutor(Executor& executor)
{
    if (auto* strandExecutor = dynamic_cast<StrandExecutor*>(&executor)) {
        m_d = strandExecutor->m_d;
    } else {
        m_d = std::make_shared<Impl>(executor);
    }
}

StrandExecutor::~StrandExecutor() = default;

Executor& StrandExecutor::originExecutor() noexcept
{
    return m_d->originExecutor;
}

void StrandExecutor::exec(Work work, ExecMode mode)
{
    m_d->exec(work, mode);
}

asio::io_context& StrandExecutor::ioCtx()
{
    return m_d->originExecutor.ioCtx();
}

}   // namespace nhope
