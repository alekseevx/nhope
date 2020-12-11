#include <algorithm>
#include <cassert>
#include <condition_variable>
#include <functional>
#include <map>
#include <memory>
#include <mutex>

#include "nhope/asyncs/ao-context.h"
#include "nhope/asyncs/reverse_lock.h"
#include "nhope/asyncs/thread-executor.h"

namespace {

using namespace nhope::asyncs;
using CancelHandler = AOContext::CancelHandler;

enum class AOContextState
{
    Open,
    Closing,
    Closed
};

struct AsyncOpertionRec
{
    CancelHandler cancelHandler;
};

}   // namespace

AsyncOperationWasCancelled::AsyncOperationWasCancelled()
  : std::runtime_error("AsyncOperationWasCancelled")
{}

class AOContext::Impl : public std::enable_shared_from_this<AOContext::Impl>
{
public:
    explicit Impl(ThreadExecutor& executor)
      : executor(executor)
    {}

    AsyncOperationId makeAsyncOperation(CancelHandler&& cancelHandler)
    {
        std::unique_lock lock(this->mutex);

        assert(this->state == AOContextState::Open);   // NOLINT

        const auto id = this->asyncOperationCounter++;
        auto& rec = this->activeAsyncOperations[id];
        rec.cancelHandler = std::move(cancelHandler);

        return id;
    }

    void asyncOperationFinished(AsyncOperationId id, std::function<void()>&& completionHandler)
    {
        std::unique_lock lock(this->mutex);

        if (this->state != AOContextState::Open) {
            return;
        }

        if (!this->isThisThreadTheThreadExecutor()) {
            // Let's transfer the call to the executor thread
            auto self = this->shared_from_this();
            this->executor.post([id, self, ch = std::move(completionHandler)]() mutable {
                self->asyncOperationFinished(id, std::move(ch));
            });
            return;
        }

        if (!this->removeAsyncOperationRec(lock, id)) {
            return;
        }

        this->callCompletionHandler(lock, completionHandler);
    }

    void close()
    {
        std::unique_lock lock(this->mutex);

        assert(this->state == AOContextState::Open);   // NOLINT

        this->state = AOContextState::Closing;
        this->waitActiveCompletionHandler(lock);
        this->cancelAsyncOperations(lock);
        this->state = AOContextState::Closed;
    }

    bool removeAsyncOperationRec([[maybe_unused]] std::unique_lock<std::mutex>& lock, AsyncOperationId id)
    {
        assert(lock.owns_lock());   // NOLINT
        return this->activeAsyncOperations.erase(id) != 0;
    }

    void callCompletionHandler(std::unique_lock<std::mutex>& lock, std::function<void()>& completionHandler)
    {
        assert(lock.owns_lock());   // NOLINT

        if (completionHandler == nullptr) {
            return;
        }

        this->hasActiveCompletionHandler = true;
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

        if (this->isThisThreadTheThreadExecutor()) {
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

        auto cancelledAsyncOperations = std::move(this->activeAsyncOperations);

        ReverseLock unlock(lock);

        for (auto& [id, cancelledAsyncOperation] : cancelledAsyncOperations) {
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

    bool isThisThreadTheThreadExecutor() const
    {
        const auto thisThreadId = std::this_thread::get_id();
        return thisThreadId == this->executor.getThreadId();
    }

    ThreadExecutor& executor;

    std::mutex mutex;

    std::condition_variable noActiveCompletionHandlerCV;
    bool hasActiveCompletionHandler = false;

    AOContextState state = AOContextState::Open;
    AsyncOperationId asyncOperationCounter = 0;
    std::map<AsyncOperationId, AsyncOpertionRec> activeAsyncOperations;
};

AOContext::AOContext(ThreadExecutor& executor)
  : m_d(std::make_shared<Impl>(executor))
{}

AOContext::~AOContext()
{
    m_d->close();
}

ThreadExecutor& AOContext::executor()
{
    return m_d->executor;
}

AOContext::AsyncOperationId AOContext::makeAsyncOperation(std::shared_ptr<Impl>& d, CancelHandler&& cancelHandler)
{
    return d->makeAsyncOperation(std::move(cancelHandler));
}

void AOContext::asyncOperationFinished(std::shared_ptr<Impl>& d, AsyncOperationId id,
                                       std::function<void()>&& completionHandler)
{
    d->asyncOperationFinished(id, std::move(completionHandler));
}
