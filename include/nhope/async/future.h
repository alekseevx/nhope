#pragma once

#include <chrono>
#include <exception>
#include <functional>
#include <future>
#include <list>
#include <memory>
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
using NextFuture = typename detail::NextFuture<T, Fn>::Type;

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

    template<typename Executor, typename Fn>
    UnwrapFuture<NextFuture<T, Fn>> then(BaseAOContext<Executor>& aoCtx, Fn&& fn)
    {
        auto state = this->detachState();
        auto nextState = std::make_shared<typename NextFuture<T, Fn>::State>();

        std::function callback = [state, nextState, fn]() mutable {
            auto result = state->getResult();
            if (detail::isException<T>(result)) {
                nextState->setException(detail::exception<T>(std::move(result)));
                return;
            }

            if constexpr (std::is_void_v<T>) {
                detail::resolveState(*nextState, std::forward<Fn>(fn));
            } else {
                detail::resolveState(*nextState, std::forward<Fn>(fn), detail::value<T>(std::move(result)));
            }
        };
        std::function cancel = [nextState] {
            nextState->setException(std::make_exception_ptr(AsyncOperationWasCancelled()));
        };

        state->setCallback(aoCtx.newAsyncOperation(std::move(callback), std::move(cancel)));

        return NextFuture<T, Fn>(nextState).unwrap();
    }

    template<typename Executor, typename Fn>
    Future fail(BaseAOContext<Executor>& aoCtx, Fn&& fn)
    {
        auto state = this->detachState();
        auto nextState = std::make_shared<Future::State>();

        std::function callback = [state, nextState, fn]() mutable {
            auto result = state->getResult();
            if (detail::isValue<T>(result)) {
                detail::resolveState(*nextState, [&result]() mutable {
                    return detail::value<T>(std::move(result));
                });
                return;
            }

            detail::resolveState(*nextState, std::forward<Fn>(fn), detail::exception<T>(std::move(result)));
        };
        std::function cancel = [nextState] {
            nextState->setException(std::make_exception_ptr(AsyncOperationWasCancelled()));
        };

        state->setCallback(aoCtx.newAsyncOperation(std::move(callback), std::move(cancel)));

        return Future(nextState);
    }

private:
    using State = detail::FutureState<T>;

private:
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
            auto state = this->detachState();
            auto unwrapState = std::make_shared<typename UnwrapFuture<T>::State>();
            detail::unwrapHelper(std::move(state), unwrapState);
            return UnwrapFuture<T>(std::move(unwrapState));
        }
    }

private:
    std::shared_ptr<State> m_state;
};

template<typename T>
class Promise final
{
public:
    Promise()
      : m_state(std::make_shared<State>())
    {}

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

private:
    std::shared_ptr<State> m_state;
};

template<typename T>
Future<T> makeReadyFuture(T&& value)
{
    Promise<T> promise;
    Future<T> future = promise.future();
    promise.setValue(std::forward<T>(value));
    return future;
}

inline Future<void> makeReadyFuture()
{
    Promise<void> promise;
    Future<void> future = promise.future();
    promise.setValue();
    return future;
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
