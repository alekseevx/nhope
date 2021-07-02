#pragma once

#include <algorithm>
#include <chrono>
#include <exception>
#include <functional>
#include <future>
#include <list>
#include <memory>
#include <system_error>
#include <thread>
#include <type_traits>
#include <utility>

#include <nhope/async/ao-context.h>
#include <nhope/async/detail/future.h>

namespace nhope {

template<typename T>
class Promise;

template<typename T>
using UnwrapFuture = typename detail::UnwrapFuture<T>::Type;

template<typename T, typename Fn>
using NextFutureState = typename detail::NextFutureState<T, Fn>::Type;

template<typename T>
inline constexpr bool isFuture = detail::isFuture<T>;

template<typename T>
class Future final
{
    friend Promise<T>;

    template<typename>
    friend class Future;

    template<typename Tp, typename UnwrappedT>
    friend void detail::unwrapHelper(std::shared_ptr<detail::FutureState<Tp>> state,
                                     std::shared_ptr<detail::FutureState<UnwrappedT>> unwrappedState);

public:
    using Type = T;

    Future() = default;

    Future(const Future&) = delete;
    Future(Future&& other) noexcept = default;
    Future& operator=(Future&& other) noexcept = default;

    [[nodiscard]] bool valid() const
    {
        return m_state != nullptr;
    }

    [[nodiscard]] bool isReady() const
    {
        return state().isReady();
    }

    T get()
    {
        auto state = this->detachState();

        state->wait();

        auto result = state->getResult();
        if (detail::isException<T>(result)) {
            detail::rethrowException<T>(result);
        }

        if constexpr (!std::is_void_v<T>) {
            return detail::value<T>(std::move(result));
        }
    }

    void wait() const
    {
        return state().wait();
    }

    [[nodiscard]] bool waitFor(std::chrono::nanoseconds time) const
    {
        return state().waitFor(time);
    }

    template<typename Fn>
    auto then(AOContext& aoCtx, Fn&& fn)   // -> UnwrapFuture<NextFuture<T, Fn>>
    {
        if constexpr (std::is_void_v<T>) {
            static_assert(std::is_invocable_v<Fn>, "Fn must be function without arguments");
        } else {
            static_assert(std::is_invocable_v<Fn, T>, "Fn must accept a single argument of same type as the Future");
        }

        using AsyncOperationHandler = detail::FutureThenAsyncOperationHandler<T, Fn>;

        auto state = this->detachState();
        auto nextState = std::make_shared<NextFutureState<T, Fn>>();
        auto handler = std::make_unique<AsyncOperationHandler>(std::forward<Fn>(fn), state, nextState);
        state->setCallback([callAOHandler = aoCtx.addAOHandler(std::move(handler))]() mutable {
            callAOHandler();
        });

        return Future<typename NextFutureState<T, Fn>::Type>(std::move(nextState)).unwrap();
    }

    template<typename Fn>
    Future fail(AOContext& aoCtx, Fn&& fn)
    {
        static_assert(std::is_invocable_v<Fn, std::exception_ptr>, "Fn must take std::exception_ptr as argument");
        static_assert(std::is_same_v<T, std::invoke_result_t<Fn, std::exception_ptr>>,
                      "Fn must return a result of same type as the Future");

        using AsyncOperationHandler = detail::FutureFailAsyncOperationHandler<T, Fn>;

        auto state = this->detachState();
        auto nextState = std::make_shared<State>();
        auto handler = std::make_unique<AsyncOperationHandler>(std::forward<Fn>(fn), state, nextState);
        state->setCallback([callAOHandler = aoCtx.addAOHandler(std::move(handler))]() mutable {
            callAOHandler();
        });

        return Future(std::move(nextState)).unwrap();
    }

private:
    using State = detail::FutureState<T>;

    explicit Future(std::shared_ptr<State> state)
      : m_state(std::move(state))
    {
        m_state->setRetrievedFlag();
    }

    const State& state() const
    {
        if (m_state == nullptr) {
            throw std::future_error(std::future_errc::no_state);
        }

        return *m_state;
    }

    std::shared_ptr<State> detachState()
    {
        if (m_state == nullptr) {
            throw std::future_error(std::future_errc::no_state);
        }

        return std::move(m_state);
    }

    UnwrapFuture<T> unwrap()
    {
        if constexpr (std::is_same_v<Future, UnwrapFuture<T>>) {
            return std::move(*this);
        } else {
            auto unwrapState = std::make_shared<typename UnwrapFuture<T>::State>();
            detail::unwrapHelper(this->detachState(), unwrapState);
            return UnwrapFuture<T>(std::move(unwrapState));
        }
    }

    std::shared_ptr<State> m_state;
};

template<typename T>
class Promise final
{
public:
    Promise()
      : m_state(std::make_shared<State>())
    {}

    ~Promise()
    {
        if (m_state && !m_state->isReady()) {
            auto exPtr = std::make_exception_ptr(std::future_error(std::future_errc::broken_promise));
            setException(std::move(exPtr));
        }
    }

    Promise(Promise&&) noexcept = default;

    Promise& operator=(Promise&&) noexcept = default;

    template<typename... Tp>
    void setValue(Tp&&... args)
    {
        m_state->setValue(std::forward<Tp>(args)...);
    }

    void setException(std::exception_ptr ex)
    {
        m_state->setException(std::move(ex));
    }

    Future<T> future()
    {
        return Future<T>(m_state);
    }

private:
    using State = detail::FutureState<T>;

    std::shared_ptr<State> m_state;
};

template<typename T, typename... Args>
Future<T> makeReadyFuture(Args&&... args)
{
    Promise<T> promise;
    promise.setValue(std::forward<Args>(args)...);
    return promise.future();
}

inline Future<void> makeReadyFuture()
{
    return makeReadyFuture<void>();
}

template<typename T, typename... Args>
void resolvePromises(std::list<Promise<T>>& promises, Args&&... args)
{
    for (auto& p : promises) {
        if constexpr (std::is_void_v<T>) {
            p.setValue();
        } else {
            T val(std::forward<Args>(args)...);
            p.setValue(std::move(val));
        }
    }
    promises.clear();
}

template<typename T, typename Fn, typename... Args>
Future<T> toThread(Fn&& fn, Args&&... args)
{
    Promise<T> promise;
    Future<T> future = promise.future();

    auto bindedFn = std::bind(fn, std::forward<Args>(args)...);
    std::thread([fn = std::move(bindedFn), promise = std::move(promise)]() mutable {
        try {
            if constexpr (std::is_void_v<T>) {
                fn();
                promise.setValue();
            } else {
                promise.setValue(fn());
            }
        } catch (...) {
            promise.setException(std::current_exception());
        }
    }).detach();

    return future;
}

}   // namespace nhope
