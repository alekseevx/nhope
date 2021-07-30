#include <cassert>
#include <condition_variable>
#include <cstddef>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <utility>

#include "nhope/async/ao-context.h"
#include "nhope/async/executor.h"
#include "nhope/async/reverse-lock.h"
#include "nhope/async/strand-executor.h"

#include "ao-handler-storage.h"
#include "make-strand.h"

namespace nhope {

namespace {

using namespace detail;

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

namespace detail {
class AOContextImpl final : public std::enable_shared_from_this<AOContextImpl>
{
    enum class AOContextState
    {
        Open,
        Closing,
        Closed
    };

public:
    explicit AOContextImpl(Executor& executor)
      : executorHolder(makeStrand(executor))
    {}

    AOHandlerCall putAOHandler(std::unique_ptr<AOHandler> handler)
    {
        std::unique_lock lock(this->mutex);

        if (this->state != AOContextState::Open) {
            throw AOContextClosed();
        }

        auto id = this->aoHandlerCounter++;
        this->aoHandlers.put(id, std::move(handler));

        return AOHandlerCall(id, weak_from_this());
    }

    void callAOHandler(AOHandlerId id)
    {
        std::unique_lock lock(this->mutex);
        if (this->state != AOContextState::Open) {
            return;
        }

        this->executorHolder->post([weakSelf = weak_from_this(), id] {
            auto self = weakSelf.lock();
            if (self == nullptr) {
                return;
            }

            std::unique_lock lock(self->mutex);
            if (self->state != AOContextState::Open) {
                return;
            }

            assert(self->hasActiveAOHandler == false);   // NOLINT

            auto handler = self->getAOHandler(lock, id);
            if (handler == nullptr) {
                return;
            }

            self->hasActiveAOHandler = true;
            self->executorThreadId = std::this_thread::get_id();
            lock.unlock();

            try {
                handler->call();
            } catch (...) {
            }
            handler.reset();

            lock.lock();
            self->hasActiveAOHandler = false;
            if (self->state == AOContextState::Closing) {
                self->noActiveHandlerCV.notify_one();
            }
        });
    }

    void close()
    {
        std::unique_lock lock(this->mutex);

        assert(this->state == AOContextState::Open);   // NOLINT

        this->state = AOContextState::Closing;
        this->waitActiveHandler(lock);
        this->state = AOContextState::Closed;

        this->cancelAOHandlers(lock);
    }

    std::unique_ptr<AOHandler> getAOHandler([[maybe_unused]] std::unique_lock<std::mutex>& lock, AOHandlerId id)
    {
        assert(lock.owns_lock());   // NOLINT
        return this->aoHandlers.get(id);
    }

    void waitActiveHandler(std::unique_lock<std::mutex>& lock)
    {
        assert(lock.owns_lock());   // NOLINT

        if (this->isThisThreadTheThreadExecutor(lock)) {
            // we can't wait, we can get an infinite loop
            return;
        }

        while (this->hasActiveAOHandler) {
            this->noActiveHandlerCV.wait(lock);
        }
    }

    void cancelAOHandlers(std::unique_lock<std::mutex>& lock)
    {
        assert(lock.owns_lock());   // NOLINT

        auto cancelledAOHandlers = std::move(this->aoHandlers);

        ReverseLock unlock(lock);
        cancelledAOHandlers.cancelAll();
    }

    [[nodiscard]] bool isThisThreadTheThreadExecutor([[maybe_unused]] std::unique_lock<std::mutex>& lock) const
    {
        assert(lock.owns_lock());   // NOLINT
        const auto thisThreadId = std::this_thread::get_id();
        return thisThreadId == executorThreadId;
    }

    ExecutorHolder executorHolder;

    std::mutex mutex;

    std::condition_variable noActiveHandlerCV;
    bool hasActiveAOHandler = false;
    std::thread::id executorThreadId{};

    AOContextState state = AOContextState::Open;
    AOHandlerId aoHandlerCounter = 0;
    AOHandlerStorage aoHandlers;
};

}   // namespace detail

AOHandlerCall::AOHandlerCall(AOHandlerId id, AOContextImplWPtr aoImpl)
  : m_id(id)
  , m_aoImpl(std::move(aoImpl))
{}

AOHandlerCall::operator bool() const
{
    return !m_aoImpl.expired();
}

void AOHandlerCall::operator()()
{
    if (auto aoImpl = m_aoImpl.lock()) {
        aoImpl->callAOHandler(m_id);
        m_aoImpl.reset();
    }
}

AOContext::AOContext(Executor& executor)
  : m_d(std::make_shared<detail::AOContextImpl>(executor))
{}

AOContext::~AOContext()
{
    m_d->close();
}

AOHandlerCall AOContext::putAOHandler(std::unique_ptr<AOHandler> handler)
{
    return m_d->putAOHandler(std::move(handler));
}

SequenceExecutor& AOContext::executor()
{
    return *m_d->executorHolder;
}

AOContextWeekRef::AOContextWeekRef(AOContext& aoCtx)
  : m_aoImpl(aoCtx.m_d)
{}

AOHandlerCall AOContextWeekRef::putAOHandler(std::unique_ptr<AOHandler> handler)
{
    auto aoImpl = m_aoImpl.lock();
    if (aoImpl == nullptr) {
        throw AOContextClosed();
    }

    return aoImpl->putAOHandler(std::move(handler));
}

}   // namespace nhope
