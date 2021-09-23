#pragma once

#include <exception>
#include <memory>
#include <thread>
#include <type_traits>
#include <utility>

#include "nhope/async/ao-context.h"
#include "nhope/async/detail/future-state.h"
#include "nhope/async/event.h"
#include "nhope/async/future-error.h"
#include "nhope/utils/detail/ref-ptr.h"

namespace nhope {

template<typename T>
class Promise;

template<typename T>
using UnwrapFuture = typename detail::UnwrapFuture<T>::Type;

template<typename T, typename Fn>
using NextFutureState = typename detail::NextFutureState<T, Fn>::Type;

template<typename T, typename Fn>
using NextFuture = Future<typename NextFutureState<T, Fn>::Type>;

template<typename T>
inline constexpr bool isFuture = detail::isFuture<T>;

/**
 * @brief A Future represents the result of an asynchronous computation.
 * 
 * @tparam T Result type
 */
template<typename T>
class Future final
{
    friend Promise<T>;

    template<typename>
    friend class Future;

    template<typename Tp, typename UnwrappedT>
    friend class detail::UnwrapperFutureCallback;

public:
    using Type = T;

    /**
     * @post isValid() == false
     * @post isWaitFuture() = false
     */
    Future() = default;

    Future(const Future&) = delete;

    /**
     * @post other.isValid() == false
     * @post other.isWaitFuture() = false
     */
    Future(Future&& other) noexcept = default;

    /**
     * @post other.isValid() == false
     * @post other.isWaitFuture() = false
     */
    Future& operator=(Future&& other) noexcept = default;

    /**
     * @brief Checks if the Future has state
     */
    [[nodiscard]] bool isValid() const
    {
        return m_state != nullptr;
    }

    /**
     * @brief Checks if the Future has result
     * @pre isValid() == true
     */
    [[nodiscard]] bool isReady() const
    {
        return state().hasResult();
    }

    /**
     * @brief Waits and returns result
     * 
     * @pre isValid() == true
     * @post isValid() == false
     * @post isWaitFuture() = true
     * @post isReady() = true
     */
    T get()
    {
        auto detachedState = this->detachState();

        if (!detachedState->hasResult()) {
            auto& futureReadyEvent = this->makeWaitFuture(*detachedState);
            futureReadyEvent.wait();
        }

        if (detachedState->hasException()) {
            std::rethrow_exception(detachedState->exception());
        }

        if constexpr (!std::is_void_v<T>) {
            return std::move(detachedState->value());
        }
    }

    /**
     * @brief Blocks until the result become available
     *
     * @pre isValid() == true
     * @post isValid() == true
     * @post isWaitFuture() == true
     */
    void wait()
    {
        auto& futureReadyEvent = this->makeWaitFuture(state());
        futureReadyEvent.wait();
    }

    /**
     * @brief Blocks until specified time has elapsed or the result become available
     *
     * @pre isValid() == true
     * @post isValid() == true
     * @post isWaitFuture() == true
     */
    [[nodiscard]] bool waitFor(std::chrono::nanoseconds time)
    {
        auto& futureReadyEvent = this->makeWaitFuture(state());
        return futureReadyEvent.waitFor(time);
    }

    /**
     * @brief Returns the next chaining Future, where fn is callback will be called
     * when this Future is succeeds.
     *
     * @param aoCtx The AOContext on which the fn should be called.
     *
     * @pre isValid() == true
     * @pre isWaitFuture() == false
     * @post isValid() == false
     *
     * @return UnwrapFuture<NextFuture<T, Fn>>
     */
    template<typename Fn>
    UnwrapFuture<NextFuture<T, Fn>> then(AOContext& aoCtx, Fn&& fn)
    {
        using FutureCallback = detail::FutureThenCallback<T, Fn>;

        if constexpr (std::is_void_v<T>) {
            static_assert(std::is_invocable_v<Fn>, "Fn must be function without arguments");
        } else {
            static_assert(std::is_invocable_v<Fn, T>, "Fn must accept a single argument of same type as the Future");
        }

        if (this->isWaitFuture()) {
            throw MakeFutureChainAfterWaitError();
        }

        auto detachedState = this->detachState();

        auto nextState = detail::makeRefPtr<NextFutureState<T, Fn>>();
        detachedState->setCallback(std::make_unique<FutureCallback>(aoCtx, std::forward<Fn>(fn), nextState));

        return NextFuture<T, Fn>(std::move(nextState)).unwrap();
    }

    template<typename Fn>
    UnwrapFuture<NextFuture<T, Fn>> then(Fn&& fn)
    {
        using FutureCallback = detail::FutureThenWithoutAOCtxCallback<T, Fn>;

        if constexpr (std::is_void_v<T>) {
            static_assert(std::is_invocable_v<Fn>, "Fn must be function without arguments");
        } else {
            static_assert(std::is_invocable_v<Fn, T>, "Fn must accept a single argument of same type as the Future");
        }

        if (this->isWaitFuture()) {
            throw MakeFutureChainAfterWaitError();
        }

        auto state = this->detachState();

        auto nextState = detail::makeRefPtr<NextFutureState<T, Fn>>();
        state->setCallback(std::make_unique<FutureCallback>(std::forward<Fn>(fn), nextState));

        return NextFuture<T, Fn>(std::move(nextState)).unwrap();
    }

    /**
     * @brief Returns the next chaining Future, where fn is callback will be called
     * when this Future fails.
     *
     * @param aoCtx The AOContext on which the fn should be called.
     *
     * @pre isValid() == true
     * @pre isWaitFuture() == false
     * @post isValid() == false
     *
     * @return Future<T>
     */
    template<typename Fn>
    Future<T> fail(AOContext& aoCtx, Fn&& fn)
    {
        using FutureCallback = detail::FutureFailCallback<T, Fn>;

        static_assert(std::is_invocable_v<Fn, std::exception_ptr>, "Fn must take std::exception_ptr as argument");
        static_assert(std::is_same_v<T, std::invoke_result_t<Fn, std::exception_ptr>>,
                      "Fn must return a result of same type as the Future");

        if (this->isWaitFuture()) {
            throw MakeFutureChainAfterWaitError();
        }

        auto detachedState = this->detachState();

        auto nextState = detail::makeRefPtr<State>();
        detachedState->setCallback(std::make_unique<FutureCallback>(aoCtx, std::forward<Fn>(fn), nextState));

        return Future(std::move(nextState)).unwrap();
    }

    template<typename Fn>
    Future<T> fail(Fn&& fn)
    {
        using FutureCallback = detail::FutureFailWithoutAOCtxCallback<T, Fn>;

        static_assert(std::is_invocable_v<Fn, std::exception_ptr>, "Fn must take std::exception_ptr as argument");
        static_assert(std::is_same_v<T, std::invoke_result_t<Fn, std::exception_ptr>>,
                      "Fn must return a result of same type as the Future");

        if (this->isWaitFuture()) {
            throw MakeFutureChainAfterWaitError();
        }

        auto state = this->detachState();

        auto nextState = detail::makeRefPtr<State>();
        state->setCallback(std::make_unique<FutureCallback>(std::forward<Fn>(fn), nextState));

        return Future(std::move(nextState)).unwrap();
    }

    /**
     * @brief Unwraps Future<Future<...<Future<T>...>> into Future<T>
     *
     * @pre isValid() == true
     * @pre isWaitFuture() == false
     * @post isValid() == false
     */
    UnwrapFuture<T> unwrap()
    {
        if (this->isWaitFuture()) {
            throw MakeFutureChainAfterWaitError();
        }

        if constexpr (std::is_same_v<Future, UnwrapFuture<T>>) {
            return std::move(*this);
        } else {
            using UnwrappedT = typename UnwrapFuture<T>::Type;
            using UnwrapFutureState = typename UnwrapFuture<T>::State;
            using FutureCallback = detail::UnwrapperFutureCallback<T, UnwrappedT>;

            auto detachedState = this->detachState();

            auto unwrapState = detail::makeRefPtr<UnwrapFutureState>();
            detachedState->setCallback(std::make_unique<FutureCallback>(unwrapState));

            return UnwrapFuture<T>(std::move(unwrapState));
        }
    }

    /**
     * @brief Checks whether was called wait or waitFor
     * 
     * If #wait or waitFor was called, then and fail must not be called.
     */
    [[nodiscard]] bool isWaitFuture() const
    {
        return m_futureReadyEvent != nullptr;
    }

private:
    using State = detail::FutureState<T>;

    /**
     * @post other.isValid() == true if state != nullptr
     * @post other.isWaitFuture() = false
     */
    explicit Future(detail::RefPtr<State> state)
      : m_state(std::move(state))
    {
        assert(m_state != nullptr);   // NOLINT
    }

    State& state()
    {
        if (m_state == nullptr) {
            throw FutureNoStateError();
        }

        return *m_state;
    }

    const State& state() const
    {
        if (m_state == nullptr) {
            throw FutureNoStateError();
        }

        return *m_state;
    }

    detail::RefPtr<State> detachState()
    {
        if (m_state == nullptr) {
            throw FutureNoStateError();
        }

        return std::move(m_state);
    }

    Event& makeWaitFuture(State& state)
    {
        using FutureCallback = detail::SetEventFutureCallback<T>;

        if (m_futureReadyEvent == nullptr) {
            m_futureReadyEvent = detail::makeRefPtr<Event>();
            state.setCallback(std::make_unique<FutureCallback>(m_futureReadyEvent));
        }

        return *m_futureReadyEvent;
    }

    detail::RefPtr<State> m_state;
    detail::RefPtr<Event> m_futureReadyEvent;
};

template<typename T>
class Promise final
{
public:
    Promise()
      : m_state(detail::makeRefPtr<State>())
    {}

    ~Promise()
    {
        if (!m_satisfiedFlag) {
            auto exPtr = std::make_exception_ptr(BrokenPromiseError());
            this->setException(std::move(exPtr));
        }
    }

    Promise(Promise&& other) noexcept
      : m_state(std::move(other.m_state))
      , m_satisfiedFlag(other.m_satisfiedFlag)
      , m_retrievedFlag(other.m_retrievedFlag)
    {
        other.m_satisfiedFlag = true;
        other.m_retrievedFlag = true;
    }

    Promise& operator=(Promise&& other) noexcept
    {
        m_state = std::move(other.m_state);
        m_satisfiedFlag = std::exchange(other.m_satisfiedFlag, true);
        m_retrievedFlag = std::exchange(other.m_retrievedFlag, true);
        return *this;
    }

    template<typename... Tp>
    void setValue(Tp&&... args)
    {
        if (m_satisfiedFlag) {
            throw PromiseAlreadySatisfiedError();
        }

        m_state->setValue(std::forward<Tp>(args)...);
        m_satisfiedFlag = true;
    }

    void setException(std::exception_ptr ex)
    {
        if (m_satisfiedFlag) {
            throw PromiseAlreadySatisfiedError();
        }

        m_state->setException(std::move(ex));
        m_satisfiedFlag = true;
    }

    Future<T> future()
    {
        if (m_retrievedFlag) {
            throw FutureAlreadyRetrievedError();
        }
        m_retrievedFlag = true;

        return Future<T>(m_state);
    }

    [[nodiscard]] bool satisfied() const noexcept
    {
        return m_satisfiedFlag;
    }

private:
    using State = detail::FutureState<T>;

    detail::RefPtr<State> m_state;
    bool m_satisfiedFlag = false;
    bool m_retrievedFlag = false;
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

template<template<typename, typename> typename Cont, typename T, typename Alloc, typename... Args>
void resolvePromises(Cont<Promise<T>, Alloc>& promises, Args&&... args)
{
    for (auto& p : promises) {
        p.setValue(std::forward<Args>(args)...);
    }
    promises.clear();
}

template<template<typename, typename> typename Cont, typename T, typename Alloc>
void rejectPromises(Cont<Promise<T>, Alloc>& promises, const std::exception_ptr& e)
{
    for (auto& p : promises) {
        p.setException(e);
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
