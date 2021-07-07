#pragma once

#include <algorithm>
#include <cassert>
#include <condition_variable>
#include <exception>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <type_traits>
#include <utility>

#include "nhope/async/ao-context.h"

namespace nhope {

template<typename T>
class Future;

namespace detail {

template<typename T>
class FutureState;

template<typename T>
struct UnwrapFuture
{
    using Type = Future<T>;
};

template<typename T>
struct UnwrapFuture<Future<T>>
{
    using Type = typename UnwrapFuture<T>::Type;
};

template<typename T, typename Fn>
struct NextFutureState
{
    using Type = FutureState<std::invoke_result_t<Fn, T>>;
};

template<typename Fn>
struct NextFutureState<void, Fn>
{
    using Type = FutureState<std::invoke_result_t<Fn>>;
};

template<typename T>
inline constexpr bool isFuture = false;

template<typename T>
inline constexpr bool isFuture<Future<T>> = true;

class Void
{};

template<typename T>
using FutureValue = std::conditional_t<std::is_void_v<T>, Void, T>;

template<typename T>
class FutureState;

template<typename T>
class FutureStateLock final
{
public:
    explicit FutureStateLock(FutureState<T>* futureState)
      : m_lock(futureState->m_mutex)
      , m_futureState(futureState)
    {}

    explicit FutureStateLock(FutureState<T>* futureState, std::unique_lock<std::mutex> lock)
      : m_lock(std::move(lock))
      , m_futureState(futureState)
    {}

    std::exception_ptr getException()
    {
        return m_futureState->getException(m_lock);
    }

    T getValue()
    {
        return m_futureState->getValue(m_lock);
    }

    void wait()
    {
        m_futureState->wait(m_lock);
    }

    void unlock()
    {
        m_lock.unlock();
    }

private:
    std::unique_lock<std::mutex> m_lock;
    FutureState<T>* m_futureState;
};

template<typename T>
class FutureCallback
{
public:
    virtual ~FutureCallback() = default;

    virtual void satisfied(FutureStateLock<T> state) = 0;
};

template<typename T>
class FutureState final
{
    friend class FutureStateLock<T>;

public:
    using Type = T;
    using Callback = std::function<void(FutureStateLock<T> state)>;

    template<typename... Args>
    void setValue(Args&&... args)
    {
        std::unique_lock lock(m_mutex);

        assert(m_value == std::nullopt && m_exception == nullptr);

        m_value.emplace(std::forward<Args>(args)...);
        this->satisfied(std::move(lock));
    }

    void setException(std::exception_ptr exception)
    {
        std::unique_lock lock(m_mutex);

        assert(m_value == std::nullopt && m_exception == nullptr);

        m_exception = std::move(exception);
        this->satisfied(std::move(lock));
    }

    template<typename Fn, typename... Args>
    void calcResult(Fn&& fn, Args&&... args)
    {
        try {
            if constexpr (std::is_void_v<T>) {
                fn(std::forward<Args>(args)...);
                this->setValue();
            } else {
                this->setValue(fn(std::forward<Args>(args)...));
            }
        } catch (...) {
            this->setException(std::current_exception());
        }
    }

    bool isReady() const
    {
        std::scoped_lock lock(m_mutex);
        return m_value != std::nullopt || m_exception != nullptr;
    }

    void wait() const
    {
        std::unique_lock lock(m_mutex);
        this->wait(lock);
    }

    bool waitFor(std::chrono::nanoseconds time) const
    {
        std::unique_lock lock(m_mutex);
        return this->waitFor(lock, time);
    }

    void setCallback(std::unique_ptr<FutureCallback<T>> callback)
    {
        assert(callback != nullptr);

        std::unique_lock lock(m_mutex);

        assert(m_waiter == std::nullopt);   // NOLINT

        if (m_value == std::nullopt && m_exception == nullptr) {
            m_callback = std::move(callback);
        } else {
            callback->satisfied(FutureStateLock<T>(this, std::move(lock)));
        }
    }

    template<typename Fn>
    auto lock(Fn&& fn)
    {
        return fn(FutureStateLock<T>(this));
    }

private:
    void wait(std::unique_lock<std::mutex>& lock) const
    {
        assert(m_callback == nullptr);

        if (m_waiter == std::nullopt) {
            m_waiter.emplace();
        }

        m_waiter->wait(lock, [this] {
            return m_value != std::nullopt || m_exception != nullptr;
        });
    }

    bool waitFor(std::unique_lock<std::mutex>& lock, std::chrono::nanoseconds time) const
    {
        assert(m_callback == nullptr);

        if (m_waiter == std::nullopt) {
            m_waiter.emplace();
        }

        return m_waiter->wait_for(lock, time, [this] {
            return m_value != std::nullopt || m_exception != nullptr;
        });
    }

    T getValue([[maybe_unused]] std::unique_lock<std::mutex>& lock)
    {
        if constexpr (std::is_void_v<T>) {
            return;
        } else {
            return std::move(*m_value);
        }
    }

    std::exception_ptr getException([[maybe_unused]] std::unique_lock<std::mutex>& lock)
    {
        return std::move(m_exception);
    }

    void satisfied(std::unique_lock<std::mutex> lock)
    {
        if (m_callback != nullptr) {
            assert(m_waiter == std::nullopt);   // NOLINT

            m_callback->satisfied(FutureStateLock<T>(this, std::move(lock)));
        } else if (m_waiter != std::nullopt) {
            m_waiter->notify_one();
        }
    }

    mutable std::mutex m_mutex;

    std::optional<FutureValue<T>> m_value;
    std::exception_ptr m_exception;

    std::unique_ptr<FutureCallback<T>> m_callback;
    mutable std::optional<std::condition_variable> m_waiter;
};

template<typename T>
class CallAOHandlerFutureCallback final : public FutureCallback<T>
{
public:
    CallAOHandlerFutureCallback(AOContext& aoCtx, std::unique_ptr<AOHandler> aoHandler)
      : m_callAOHandler(aoCtx.putAOHandler(std::move(aoHandler)))
    {}

    void satisfied(FutureStateLock<T> /*unused*/) override
    {
        m_callAOHandler();
    }

private:
    AOHandlerCall m_callAOHandler;
};

template<typename T, typename UnwrappedT>
class UnwrapperFutureCallback final : public FutureCallback<T>
{
public:
    UnwrapperFutureCallback(std::shared_ptr<detail::FutureState<UnwrappedT>> unwrapState)
      : m_unwrapState(std::move(unwrapState))
    {}

    void satisfied(FutureStateLock<T> state) override
    {
        if (auto exPtr = state.getException()) {
            state.unlock();

            m_unwrapState->setException(std::move(exPtr));
            return;
        }

        if constexpr (isFuture<T>) {
            using NextT = typename T::Type;
            using NextFutureCallaback = UnwrapperFutureCallback<NextT, UnwrappedT>;

            auto nextState = state.getValue().detachState();
            state.unlock();

            auto nextCallback = std::make_unique<NextFutureCallaback>(std::move(m_unwrapState));
            nextState->setCallback(std::move(nextCallback));
        } else if constexpr (!std::is_void_v<UnwrappedT>) {
            auto value = state.getValue();
            state.unlock();

            m_unwrapState->setValue(std::move(value));
        } else {
            state.unlock();

            m_unwrapState->setValue();
        }
    }

private:
    std::shared_ptr<detail::FutureState<UnwrappedT>> m_unwrapState;
};

template<typename T, typename Fn>
class FutureThenAOHandler final : public AOHandler
{
public:
    using NextFutureState = typename NextFutureState<T, Fn>::Type;

    template<typename F>
    FutureThenAOHandler(F&& f, std::shared_ptr<FutureState<T>> futureState,
                        std::shared_ptr<NextFutureState> nextFutureState)
      : m_futureState(std::move(futureState))
      , m_nextFutureState(std::move(nextFutureState))
      , m_fn(std::forward<F>(f))
    {}

    void call() override
    {
        m_futureState->lock([this](FutureStateLock<T> state) {
            if (auto exPtr = state.getException()) {
                state.unlock();
                m_nextFutureState->setException(std::move(exPtr));
                return;
            }

            if constexpr (std::is_void_v<T>) {
                state.unlock();

                m_nextFutureState->calcResult(std::move(m_fn));
            } else {
                auto value = state.getValue();
                state.unlock();

                m_nextFutureState->calcResult(m_fn, std::move(value));
            }
        });
    }

    void cancel() override
    {
        auto exPtr = std::make_exception_ptr(AsyncOperationWasCancelled());
        m_nextFutureState->setException(std::move(exPtr));
    }

private:
    std::shared_ptr<FutureState<T>> m_futureState;
    std::shared_ptr<NextFutureState> m_nextFutureState;
    Fn m_fn;
};

template<typename T, typename Fn>
class FutureFailAOHandler final : public AOHandler
{
public:
    template<typename F>
    FutureFailAOHandler(F&& f, std::shared_ptr<FutureState<T>> futureState,
                        std::shared_ptr<FutureState<T>> nextFutureState)
      : m_futureState(std::move(futureState))
      , m_nextFutureState(std::move(nextFutureState))
      , m_fn(std::forward<F>(f))
    {}

    void call() override
    {
        m_futureState->lock([this](FutureStateLock<T> state) {
            if (auto exPtr = state.getException()) {
                state.unlock();
                m_nextFutureState->calcResult(std::move(m_fn), std::move(exPtr));
                return;
            }

            if constexpr (std::is_void_v<T>) {
                state.unlock();
                m_nextFutureState->setValue();
            } else {
                T value = state.getValue();
                state.unlock();
                m_nextFutureState->setValue(std::move(value));
            }
        });
    }

    void cancel() override
    {
        auto exPtr = std::make_exception_ptr(AsyncOperationWasCancelled());
        m_nextFutureState->setException(std::move(exPtr));
    }

private:
    std::shared_ptr<FutureState<T>> m_futureState;
    std::shared_ptr<FutureState<T>> m_nextFutureState;
    Fn m_fn;
};

}   // namespace detail
}   // namespace nhope
