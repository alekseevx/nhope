#pragma once

#include <chrono>
#include <exception>
#include <list>
#include <memory>
#include <system_error>
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
    friend class detail::UnwrapperFutureCallback;

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
        return state->lock([](detail::FutureStateLock<T> state) {
            state.wait();

            if (auto exPtr = state.getException()) {
                std::rethrow_exception(exPtr);
            }

            return state.getValue();
        });
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
        using AOHandler = detail::FutureThenAOHandler<T, Fn>;
        using FutureCallback = detail::CallAOHandlerFutureCallback<T>;

        if constexpr (std::is_void_v<T>) {
            static_assert(std::is_invocable_v<Fn>, "Fn must be function without arguments");
        } else {
            static_assert(std::is_invocable_v<Fn, T>, "Fn must accept a single argument of same type as the Future");
        }

        auto state = this->detachState();
        auto nextState = std::make_shared<NextFutureState<T, Fn>>();
        auto aoHandler = std::make_unique<AOHandler>(std::forward<Fn>(fn), state, nextState);
        state->setCallback(std::make_unique<FutureCallback>(aoCtx, std::move(aoHandler)));

        return Future<typename NextFutureState<T, Fn>::Type>(std::move(nextState)).unwrap();
    }

    template<typename Fn>
    Future fail(AOContext& aoCtx, Fn&& fn)
    {
        using AOHandler = detail::FutureFailAOHandler<T, Fn>;
        using FutureCallback = detail::CallAOHandlerFutureCallback<T>;

        static_assert(std::is_invocable_v<Fn, std::exception_ptr>, "Fn must take std::exception_ptr as argument");
        static_assert(std::is_same_v<T, std::invoke_result_t<Fn, std::exception_ptr>>,
                      "Fn must return a result of same type as the Future");

        auto state = this->detachState();
        auto nextState = std::make_shared<State>();
        auto aoHandler = std::make_unique<AOHandler>(std::forward<Fn>(fn), state, nextState);
        state->setCallback(std::make_unique<FutureCallback>(aoCtx, std::move(aoHandler)));

        return Future(std::move(nextState)).unwrap();
    }

    UnwrapFuture<T> unwrap()
    {
        if constexpr (std::is_same_v<Future, UnwrapFuture<T>>) {
            return std::move(*this);
        } else {
            using UnwrappedT = typename UnwrapFuture<T>::Type;
            using UnwrapFutureState = typename UnwrapFuture<T>::State;
            using FutureCallback = detail::UnwrapperFutureCallback<T, UnwrappedT>;

            auto state = this->detachState();
            auto unwrapState = std::make_shared<UnwrapFutureState>();
            state->setCallback(std::make_unique<FutureCallback>(unwrapState));

            return UnwrapFuture<T>(std::move(unwrapState));
        }
    }

private:
    using State = detail::FutureState<T>;

    explicit Future(std::shared_ptr<State> state)
      : m_state(std::move(state))
    {
        assert(m_state != nullptr);   // NOLINT
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
        if (!m_satisfiedFlag) {
            auto exPtr = std::make_exception_ptr(std::future_error(std::future_errc::broken_promise));
            setException(std::move(exPtr));
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
        m_state = std::exchange(other.m_state, nullptr);
        m_satisfiedFlag = std::exchange(other.m_satisfiedFlag, true);
        m_retrievedFlag = std::exchange(other.m_retrievedFlag, true);
        return *this;
    }

    template<typename... Tp>
    void setValue(Tp&&... args)
    {
        if (m_satisfiedFlag) {
            throw std::future_error(std::future_errc::promise_already_satisfied);
        }

        m_state->setValue(std::forward<Tp>(args)...);
        m_satisfiedFlag = true;
    }

    void setException(std::exception_ptr ex)
    {
        if (m_satisfiedFlag) {
            throw std::future_error(std::future_errc::promise_already_satisfied);
        }

        m_state->setException(std::move(ex));
        m_satisfiedFlag = true;
    }

    Future<T> future()
    {
        if (m_retrievedFlag) {
            throw std::future_error(std::future_errc::future_already_retrieved);
        }
        m_retrievedFlag = true;

        return Future<T>(m_state);
    }

private:
    using State = detail::FutureState<T>;

    std::shared_ptr<State> m_state;
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
