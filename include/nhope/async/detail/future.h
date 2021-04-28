#pragma once

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

using Void = std::monostate;

template<typename T>
using FutureValue = std::conditional_t<std::is_void_v<T>, Void, T>;

template<typename T>
using FutureResult = std::variant<FutureValue<T>, std::exception_ptr>;

template<typename T>
inline constexpr bool isValue(const FutureResult<T>& result) noexcept
{
    return result.index() == 0;
}

template<typename T>
inline constexpr bool isException(const FutureResult<T>& result) noexcept
{
    return result.index() == 1;
}

template<typename T>
[[noreturn]] void rethrowException(const FutureResult<T>& result)
{
    std::rethrow_exception(std::get<1>(result));
}

template<typename T>
T value(FutureResult<T>&& result)
{
    if constexpr (std::is_void_v<T>) {
        return (void)std::get<0>(std::move(result));
    } else {
        return std::get<0>(std::move(result));
    }
}

template<typename T>
std::exception_ptr exception(FutureResult<T>&& result)
{
    return std::get<1>(std::move(result));
}

template<typename T>
class FutureState final
{
public:
    using Type = T;
    using Result = FutureResult<T>;
    using ResultStorage = std::optional<Result>;

    using Callback = std::function<void()>;

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
        if (m_resultStorage != std::nullopt) {
            throw std::future_error(std::future_errc::promise_already_satisfied);
        }

        m_resultStorage = std::move(result);

        m_waiter.notify_one();
        this->doCallback(lock);
    }

    Result getResult()
    {
        std::scoped_lock lock(m_mutex);
        return std::move(*m_resultStorage);
    }

    bool isReady() const
    {
        std::scoped_lock lock(m_mutex);
        return m_resultStorage != std::nullopt;
    }

    void wait() const
    {
        std::unique_lock lock(m_mutex);
        m_waiter.wait(lock, [this] {
            return m_resultStorage != std::nullopt;
        });
    }

    bool waitFor(std::chrono::nanoseconds time) const
    {
        std::unique_lock lock(m_mutex);
        m_waiter.wait_for(lock, time, [this] {
            return m_resultStorage != std::nullopt;
        });
        return m_resultStorage != std::nullopt;
    }

    void setCallback(Callback&& callback)
    {
        std::unique_lock lock(m_mutex);
        m_callback = std::move(callback);

        if (m_resultStorage != std::nullopt) {
            this->doCallback(lock);
        }
    }

    void setRetrievedFlag()
    {
        std::scoped_lock lock(m_mutex);
        if (m_retrievedFlag) {
            throw std::future_error(std::future_errc::future_already_retrieved);
        }
        m_retrievedFlag = true;
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
        auto result = state->getResult();
        if (isException<T>(result)) {
            unwrapState->setException(exception<T>(std::move(result)));
            return;
        }

        if constexpr (isFuture<T>) {
            auto nextState = value<T>(std::move(result)).detachState();
            unwrapHelper(nextState, unwrapState);
        } else if constexpr (!std::is_void_v<UnwrappedT>) {
            unwrapState->setValue(value<T>(std::move(result)));
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
            fn(std::forward<Args>(args)...);
            state.setValue();
        } else {
            state.setValue(fn(std::forward<Args>(args)...));
        }
    } catch (...) {
        state.setException(std::current_exception());
    }
}

}   // namespace detail
}   // namespace nhope
