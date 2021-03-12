#pragma once

#include "nhope/async/ao-context.h"
#include <condition_variable>
#include <exception>
#include <functional>
#include <future>
#include <mutex>
#include <optional>
#include <utility>
#include <variant>
#include <type_traits>

#include <nhope/async/reverse_lock.h>

namespace nhope {

template<typename T>
class Future;

namespace detail {

template<typename T>
struct FutureResultType
{
    using Type = std::variant<T, std::exception_ptr>;
};

template<>
struct FutureResultType<void>
{
    using Type = std::variant<std::monostate, std::exception_ptr>;
};

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
struct NextFuture
{
    using Type = Future<std::invoke_result_t<Fn, T>>;
};

template<typename Fn>
struct NextFuture<void, Fn>
{
    using Type = Future<std::invoke_result_t<Fn>>;
};

template<typename T>
inline constexpr bool isFuture = false;

template<typename T>
inline constexpr bool isFuture<Future<T>> = true;

template<typename T>
class FutureState final
{
public:
    using Callback = std::function<void()>;

    using Type = T;
    using Result = typename FutureResultType<T>::Type;
    using ResultStorage = std::optional<Result>;

    template<typename... Args>
    void setValue(Args&&... args)
    {
        this->setResult(Result(std::in_place_index<0>, std::forward<Args>(args)...));
    }

    void setException(std::exception_ptr exception)
    {
        this->setResult(Result(std::in_place_index<1>, std::move(exception)));
    }

    void setResult(Result&& result)
    {
        std::unique_lock lock(m_mutex);
        if (this->m_resultStorage != std::nullopt) {
            throw std::future_error(std::future_errc::promise_already_satisfied);
        }

        m_resultStorage = std::move(result);

        m_waiter.notify_one();
        this->doCallback(lock);
    }

    T getValue()
    {
        std::unique_lock lock(m_mutex);
        if constexpr (std::is_void_v<T>) {
            (void)std::get<0>(*m_resultStorage);
            return;
        } else {
            return std::move(std::get<0>(*m_resultStorage));
        }
    }

    std::exception_ptr getException()
    {
        std::unique_lock lock(m_mutex);
        return std::move(std::get<1>(*m_resultStorage));
    }

    bool hasValue() const
    {
        std::unique_lock lock(m_mutex);
        if (this->m_resultStorage == std::nullopt) {
            return false;
        }
        return this->m_resultStorage->index() == 0;
    }

    bool isReady() const
    {
        std::unique_lock lock(m_mutex);
        return this->m_resultStorage != std::nullopt;
    }

    bool hasException() const
    {
        std::unique_lock lock(m_mutex);
        if (this->m_resultStorage == std::nullopt) {
            return false;
        }
        return this->m_resultStorage->index() == 1;
    }

    void wait() const
    {
        std::unique_lock lock(m_mutex);
        m_waiter.wait(lock, [this] {
            return this->m_resultStorage != std::nullopt;
        });
    }

    bool waitFor(std::chrono::nanoseconds time) const
    {
        std::unique_lock lock(m_mutex);
        this->m_waiter.wait_for(lock, time, [this] {
            return this->m_resultStorage != std::nullopt;
        });
        return this->m_resultStorage != std::nullopt;
    }

    void setCallback(Callback&& callback)
    {
        std::unique_lock lock(m_mutex);
        this->m_callback = std::move(callback);

        if (m_resultStorage != std::nullopt) {
            this->doCallback(lock);
        }
    }

    void setRetrievedFlag()
    {
        std::unique_lock lock(m_mutex);
        if (this->m_retrievedFlag) {
            throw std::future_error(std::future_errc::future_already_retrieved);
        }
        this->m_retrievedFlag = true;
    }

private:
    void doCallback(std::unique_lock<std::mutex>& lock)
    {
        if (m_callback == nullptr) {
            return;
        }

        Callback callback = std::move(m_callback);

        ReverseLock unlock(lock);
        callback();
    }

private:
    mutable std::mutex m_mutex;

    bool m_retrievedFlag = false;

    ResultStorage m_resultStorage;

    Callback m_callback;
    mutable std::condition_variable m_waiter;
};

template<typename T, typename UnwrappedT>
void unwrapHelper(std::shared_ptr<detail::FutureState<T>> state,
                  std::shared_ptr<detail::FutureState<UnwrappedT>> unwrapState)
{
    state->setCallback([state, unwrapState] {
        if (state->hasException()) {
            unwrapState->setException(state->getException());
            return;
        }

        if constexpr (isFuture<T>) {
            auto nextState = state->getValue().detachState();
            unwrapHelper(nextState, unwrapState);
        } else if constexpr (!std::is_void_v<UnwrappedT>) {
            unwrapState->setValue(state->getValue());
        } else {
            unwrapState->setValue();
        }
    });
}

template<typename T, typename Fn, typename... Args>
void resolveState(FutureState<T>& state, Fn&& fn, Args&&... args)
{
    try {
        if constexpr (std::is_void_v<T>) {
            fn(std::forward(args)...);
            state.setValue();
        } else {
            state.setValue(fn(std::forward(args)...));
        }
    } catch (...) {
        state.setException(std::current_exception());
    }
}

}   // namespace detail
}   // namespace nhope
