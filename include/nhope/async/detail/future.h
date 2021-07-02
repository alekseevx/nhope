#pragma once

#include <algorithm>
#include <cassert>
#include <condition_variable>
#include <exception>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <type_traits>
#include <utility>
#include <variant>

#include <nhope/async/ao-context.h>
#include <nhope/async/reverse_lock.h>

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

        if (m_callback != nullptr) {
            assert(m_waitCount == 0);

            auto callback = std::move(m_callback);
            lock.unlock();

            callback();
        } else if (m_waitCount != 0) {
            m_waiter.notify_one();
        }
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

        assert(m_callback == nullptr);

        ++m_waitCount;
        m_waiter.wait(lock, [this] {
            return m_resultStorage != std::nullopt;
        });
        --m_waitCount;
    }

    bool waitFor(std::chrono::nanoseconds time) const
    {
        std::unique_lock lock(m_mutex);

        assert(m_callback == nullptr);

        ++m_waitCount;
        m_waiter.wait_for(lock, time, [this] {
            return m_resultStorage != std::nullopt;
        });
        --m_waitCount;
        return m_resultStorage != std::nullopt;
    }

    void setCallback(Callback&& callback)
    {
        assert(callback != nullptr);

        std::unique_lock lock(m_mutex);

        assert(m_waitCount == 0);

        if (m_resultStorage == std::nullopt) {
            m_callback = std::move(callback);
        } else {
            lock.unlock();
            callback();
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
    mutable std::mutex m_mutex;

    bool m_retrievedFlag = false;

    ResultStorage m_resultStorage;

    Callback m_callback;
    mutable std::condition_variable m_waiter;
    mutable int m_waitCount = 0;
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

template<typename T, typename Fn>
class FutureThenAsyncOperationHandler final : public AOHandler
{
public:
    using NextFutureState = typename NextFutureState<T, Fn>::Type;

    template<typename F>
    FutureThenAsyncOperationHandler(F&& f, std::shared_ptr<FutureState<T>> futureState,
                                    std::shared_ptr<NextFutureState> nextFutureState)
      : m_futureState(std::move(futureState))
      , m_nextFutureState(std::move(nextFutureState))
      , m_fn(std::forward<F>(f))
    {}

    void call() override
    {
        auto result = m_futureState->getResult();
        if (isException<T>(result)) {
            m_nextFutureState->setException(exception<T>(std::move(result)));
            return;
        }

        if constexpr (std::is_void_v<T>) {
            resolveState(*m_nextFutureState, m_fn);
        } else {
            resolveState(*m_nextFutureState, m_fn, detail::value<T>(std::move(result)));
        }
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
class FutureFailAsyncOperationHandler final : public AOHandler
{
public:
    template<typename F>
    FutureFailAsyncOperationHandler(F&& f, std::shared_ptr<FutureState<T>> futureState,
                                    std::shared_ptr<FutureState<T>> nextFutureState)
      : m_futureState(std::move(futureState))
      , m_nextFutureState(std::move(nextFutureState))
      , m_fn(std::forward<F>(f))
    {}

    void call() override
    {
        auto result = m_futureState->getResult();
        if (isValue<T>(result)) {
            resolveState(*m_nextFutureState, [&result]() mutable {
                return detail::value<T>(std::move(result));
            });
            return;
        }

        detail::resolveState(*m_nextFutureState, m_fn, exception<T>(std::move(result)));
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
