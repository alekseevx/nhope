#include <cassert>
#include <condition_variable>
#include <map>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <utility>

#include "nhope/async/ao-context.h"
#include "nhope/async/executor.h"
#include "nhope/async/reverse_lock.h"
#include "nhope/async/strand-executor.h"

namespace nhope {

namespace {

enum HolderOwnMode
{
    HolderOwns,
    HolderNotOwns
};

class HolderDeleter final
{
public:
    HolderDeleter(HolderOwnMode ownMode)
      : m_ownMode(ownMode)
    {}

    void operator()(SequenceExecutor* executor) const
    {
        if (m_ownMode == HolderOwns) {
            delete executor;   // NOLINT(cppcoreguidelines-owning-memory)
        }
    }

private:
    const HolderOwnMode m_ownMode;
};

using ExecutorHolder = std::unique_ptr<SequenceExecutor, HolderDeleter>;

ExecutorHolder strand(Executor& executor)
{
    /* Small optimization. We create StrandExecutor only if passed executor is not SequenceExecutor */
    if (auto* seqExecutor = dynamic_cast<SequenceExecutor*>(&executor)) {
        return ExecutorHolder{seqExecutor, HolderNotOwns};
    }
    return ExecutorHolder{new StrandExecutor(executor), HolderOwns};
}

}   // namespace

AsyncOperationWasCancelled::AsyncOperationWasCancelled()
  : std::runtime_error("AsyncOperationWasCancelled")
{}

AsyncOperationWasCancelled::AsyncOperationWasCancelled(std::string_view errMessage)
  : std::runtime_error(errMessage.data())
{}

AOContextClosed::AOContextClosed()
  : std::runtime_error("AOContextClosed")
{}

class AOContext::Impl final : public std::enable_shared_from_this<Impl>
{
    enum class AOContextState
    {
        Open,
        Closing,
        Closed
    };

    struct AsyncOperationRec
    {
        CancelHandler cancelHandler;
    };

public:
    explicit Impl(Executor& executor)
      : executorHolder(strand(executor))
    {}

    AsyncOperationId makeAsyncOperation(CancelHandler&& cancelHandler)
    {
        std::unique_lock lock(this->mutex);

        if (this->state == AOContextState::Closed) {
            throw AOContextClosed();
        }

        const auto id = this->asyncOperationCounter++;
        auto& rec = this->activeAsyncOperations[id];
        rec.cancelHandler = std::move(cancelHandler);

        return id;
    }

    void asyncOperationFinished(AsyncOperationId id, std::function<void()> completionHandler)
    {
        std::unique_lock lock(this->mutex);
        if (this->state != AOContextState::Open) {
            return;
        }

        /* To avoid recursion, let's process end of operation in a delayed call */
        const auto self = this->shared_from_this();
        this->executorHolder->post([id, self, completionHandler = std::move(completionHandler)]() mutable {
            std::unique_lock lock(self->mutex);
            if (self->state != AOContextState::Open) {
                return;
            }

            if (!self->removeAsyncOperationRec(lock, id)) {
                return;
            }

            self->callCompletionHandler(lock, completionHandler);
        });
    }

    void close()
    {
        std::unique_lock lock(this->mutex);

        assert(this->state == AOContextState::Open);   // NOLINT

        this->state = AOContextState::Closing;
        this->waitActiveCompletionHandler(lock);
        this->state = AOContextState::Closed;

        this->cancelAsyncOperations(lock);
    }

    bool removeAsyncOperationRec([[maybe_unused]] std::unique_lock<std::mutex>& lock, AsyncOperationId id)
    {
        assert(lock.owns_lock());   // NOLINT
        return this->activeAsyncOperations.erase(id) != 0;
    }

    void callCompletionHandler(std::unique_lock<std::mutex>& lock, std::function<void()>& completionHandler)
    {
        assert(lock.owns_lock());                            // NOLINT
        assert(this->hasActiveCompletionHandler == false);   // NOLINT

        if (completionHandler == nullptr) {
            return;
        }

        this->hasActiveCompletionHandler = true;
        this->executorThreadId = std::this_thread::get_id();
        lock.unlock();

        try {
            completionHandler();
        } catch (...) {
            // FIXME: Logging
        }

        lock.lock();
        this->hasActiveCompletionHandler = false;
        this->noActiveCompletionHandlerCV.notify_all();
    }

    void waitActiveCompletionHandler(std::unique_lock<std::mutex>& lock)
    {
        assert(lock.owns_lock());   // NOLINT

        if (this->isThisThreadTheThreadExecutor(lock)) {
            // we can't wait, we can get an infinite loop
            return;
        }

        while (this->hasActiveCompletionHandler) {
            this->noActiveCompletionHandlerCV.wait(lock);
        }
    }

    void cancelAsyncOperations(std::unique_lock<std::mutex>& lock)
    {
        assert(lock.owns_lock());   // NOLINT

        const auto cancelledAsyncOperations = std::move(this->activeAsyncOperations);

        ReverseLock unlock(lock);

        for (const auto& [id, cancelledAsyncOperation] : cancelledAsyncOperations) {
            if (cancelledAsyncOperation.cancelHandler == nullptr) {
                continue;
            }

            try {
                cancelledAsyncOperation.cancelHandler();
            } catch (...) {
                // FIXME: Logging
            }
        }
    }

    [[nodiscard]] bool isThisThreadTheThreadExecutor([[maybe_unused]] std::unique_lock<std::mutex>& lock) const
    {
        assert(lock.owns_lock());   // NOLINT
        const auto thisThreadId = std::this_thread::get_id();
        return thisThreadId == executorThreadId;
    }

    ExecutorHolder executorHolder;

    std::mutex mutex;

    std::condition_variable noActiveCompletionHandlerCV;
    bool hasActiveCompletionHandler = false;
    std::thread::id executorThreadId{};

    AOContextState state = AOContextState::Open;
    AsyncOperationId asyncOperationCounter = 0;
    std::map<AsyncOperationId, AsyncOperationRec> activeAsyncOperations;
};

AOContext::AOContext(Executor& executor)
  : m_d(std::make_shared<Impl>(executor))
{}

AOContext::~AOContext()
{
    m_d->close();
}

SequenceExecutor& AOContext::executor()
{
    return *m_d->executorHolder;
}

AOContext::AsyncOperationId AOContext::makeAsyncOperation(Impl& d, CancelHandler&& cancelHandler)
{
    return d.makeAsyncOperation(std::move(cancelHandler));
}

void AOContext::asyncOperationFinished(Impl& d, AsyncOperationId id, std::function<void()> completionHandler)
{
    d.asyncOperationFinished(id, std::move(completionHandler));
}

}   // namespace nhope
