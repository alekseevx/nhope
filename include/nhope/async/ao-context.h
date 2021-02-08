#pragma once

#include <cstdint>
#include <memory>
#include <stdexcept>
#include <utility>
#include <string_view>
#include <algorithm>
#include <cassert>
#include <condition_variable>
#include <functional>
#include <map>
#include <mutex>
#include <thread>

#include "nhope/async/reverse_lock.h"

// FIXME: get rid of boost::exception_detail::clone_base
#include <boost/exception/exception.hpp>

namespace nhope {

class ThreadExecutor;

class AsyncOperationWasCancelled
  : public std::runtime_error
  // FIXME: get rid of boost::exception_detail::clone_base
  , public virtual boost::exception_detail::clone_base
{
public:
    AsyncOperationWasCancelled();
    explicit AsyncOperationWasCancelled(std::string_view errMessage);

public:   // FIXME: get rid of boost::exception_detail::clone_base
    [[nodiscard]] AsyncOperationWasCancelled* clone() const override;
    void rethrow() const override;
};

/**
 * @class AOContext
 *
 * @brief Контекст для выполнения асинхронных операций на заданном ThreadExecutor
 * 
 * AOContext решает следующие задачи:
 * - обеспечивает вызов CompletionHandler в ThreadExecutor
 * - гарантирует, что при уничтожении контекста все асинхронные операции, запущенные
 *   на контексте, будут отменены а их CompletionHandler вызваны не будут
 *
 */

template<class Executor>
class BaseAOContext final
{
    template<typename... Args>
    using CompletionHandler = std::function<void(Args...)>;
    using CancelHandler = std::function<void()>;
    using AsyncOperationId = std::uint64_t;

    class Impl : public std::enable_shared_from_this<Impl>
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
          : executor(executor)
        {}

        AsyncOperationId makeAsyncOperation(CancelHandler cancelHandler)
        {
            std::unique_lock lock(this->mutex);

            assert(this->state != AOContextState::Closed);   // NOLINT

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

        [[nodiscard]] bool isThisThreadTheThreadExecutor() const
        {
            const auto thisThreadId = std::this_thread::get_id();
            return thisThreadId == this->executor.getThreadId();
        }

        Executor& executor;

        std::mutex mutex;

        std::condition_variable noActiveCompletionHandlerCV;
        bool hasActiveCompletionHandler = false;

        AOContextState state = AOContextState::Open;
        AsyncOperationId asyncOperationCounter = 0;
        std::map<AsyncOperationId, AsyncOperationRec> activeAsyncOperations;
    };

public:
    BaseAOContext(const BaseAOContext&) = delete;
    BaseAOContext& operator=(const BaseAOContext&) = delete;

    BaseAOContext(BaseAOContext&&) noexcept = default;
    BaseAOContext& operator=(BaseAOContext&&) = delete;

    explicit BaseAOContext(Executor& executor)
      : m_d(std::make_shared<Impl>(executor))
    {}

    ~BaseAOContext()
    {
        m_d->close();
    }

    Executor& executor()
    {
        return m_d->executor;
    }

    /**
     * @brief Функция для создания асинхронной операции
     * 
     * @param completionHandler пользовательский обработчик окончания асинхронной операции.
                                Вызывается только в потоке ThreadExecutor-а.
     * @param cancelHandler     обработчик отмены асинхронной операции. Вызывается в том потоке, где уничтожается контекст.
     *
     * @retval функциональный объект, который должен быть вызван по завершении асинхронной операции.
     */
    template<typename... CompletionArgs>
    CompletionHandler<CompletionArgs...> newAsyncOperation(CompletionHandler<CompletionArgs...> completionHandler,
                                                           CancelHandler cancelHandler)
    {
        auto id = makeAsyncOperation(m_d, std::move(cancelHandler));
        return [id, d = this->m_d, ch = std::move(completionHandler)](CompletionArgs... args) mutable {
            auto packedCH = std::bind(std::move(ch), std::forward<CompletionArgs>(args)...);
            asyncOperationFinished(d, id, std::move(packedCH));
        };
    }

private:
    static AsyncOperationId makeAsyncOperation(std::shared_ptr<Impl>& d, CancelHandler cancelHandler)
    {
        return d->makeAsyncOperation(std::move(cancelHandler));
    }
    static void asyncOperationFinished(std::shared_ptr<Impl>& d, AsyncOperationId id,
                                       std::function<void()> completionHandler)
    {
        d->asyncOperationFinished(id, std::move(completionHandler));
    }

    std::shared_ptr<Impl> m_d;
};

using AOContext = BaseAOContext<ThreadExecutor>;

}   // namespace nhope
