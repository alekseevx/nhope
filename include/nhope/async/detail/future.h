#pragma once

#include <atomic>
#include <cassert>
#include <exception>
#include <memory>
#include <optional>
#include <type_traits>
#include <utility>

#include "nhope/async/ao-context.h"
#include "nhope/async/event.h"
#include "nhope/utils/detail/ref-ptr.h"
#include "nhope/utils/detail/compiler.h"

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
class FutureCallback
{
public:
    virtual ~FutureCallback() = default;

    virtual void futureReady(FutureState<T>& state) = 0;
};

enum FutureFlag : int
{
    HasResult = 1 << 0,
    HasCallback = 1 << 1,
};

template<typename T>
class FutureState final
{
public:
    using Type = T;

    template<typename... Args>
    void setValue(Args&&... args)
    {
        m_value.emplace(std::forward<Args>(args)...);
        this->setFlag(FutureFlag::HasResult);
    }

    FutureValue<T>& getValue()
    {
        return m_value.value();
    }

    void setException(std::exception_ptr exception)
    {
        m_exception.emplace(std::move(exception));
        this->setFlag(FutureFlag::HasResult);
    }

    [[nodiscard]] bool hasException() const
    {
        return m_exception.has_value();
    }

    std::exception_ptr& getException()
    {
        return m_exception.value();
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

    [[nodiscard]] bool hasResult() const
    {
        const int flags = m_flags.load(std::memory_order_acquire);
        return (flags & FutureFlag::HasResult) != 0;
    }

    void setCallback(std::unique_ptr<FutureCallback<T>> callback)
    {
        m_callback = std::move(callback);
        this->setFlag(FutureFlag::HasCallback);
    }

    [[nodiscard]] bool hasCallback() const
    {
        const int flags = m_flags.load(std::memory_order_acquire);
        return (flags & FutureFlag::HasCallback) != 0;
    }

private:
    void setFlag(FutureFlag flag)
    {
        assert((m_flags & flag) == 0);   //  The flag is not set yet.

        int curFlags = m_flags.load(std::memory_order_acquire);
        if (curFlags == 0) {
            bool ok{};
            if constexpr (isClang && isThreadSanitizer) {
                //  Clang TSAN ignores the passed failure order and infers failure order from
                //  success order in atomic compare-exchange operations, which is broken for
                //  cases like success-release/failure-acquire (https://github.com/facebook/folly.git).
                ok = m_flags.compare_exchange_strong(curFlags, flag, std::memory_order_acq_rel);
            } else {
                ok = m_flags.compare_exchange_strong(curFlags, flag, std::memory_order_release,   //
                                                     std::memory_order_acquire);
            }
            if (ok) {
                // So far, only one flag is set.
                return;
            }
        }

        assert((curFlags | flag) == (FutureFlag::HasResult | FutureFlag::HasCallback));
        m_flags.store(FutureFlag::HasResult | FutureFlag::HasCallback, std::memory_order_relaxed);

        // Now both flags are set.
        m_callback->futureReady(*this);
    }

    std::atomic<int> m_flags = 0;

    std::optional<FutureValue<T>> m_value;
    std::optional<std::exception_ptr> m_exception;

    std::unique_ptr<FutureCallback<T>> m_callback;
};

template<typename T>
class CallAOHandlerFutureCallback final : public FutureCallback<T>
{
public:
    CallAOHandlerFutureCallback(AOContext& aoCtx, std::unique_ptr<AOHandler> aoHandler)
      : m_callAOHandler(aoCtx.putAOHandler(std::move(aoHandler)))
    {}

    void futureReady(FutureState<T>& /*unused*/) override
    {
        m_callAOHandler();
    }

private:
    AOHandlerCall m_callAOHandler;
};

template<typename T>
class SetEventFutureCallback final : public FutureCallback<T>
{
public:
    explicit SetEventFutureCallback(RefPtr<Event> event)
      : m_event(std::move(event))
    {}

    void futureReady(FutureState<T>& /*unused*/) override
    {
        m_event->set();
    }

private:
    RefPtr<Event> m_event;
};

template<typename T, typename UnwrappedT>
class UnwrapperFutureCallback final : public FutureCallback<T>
{
public:
    UnwrapperFutureCallback(RefPtr<detail::FutureState<UnwrappedT>> unwrapState)
      : m_unwrapState(std::move(unwrapState))
    {}

    void futureReady(FutureState<T>& state) override
    {
        if (state.hasException()) {
            auto& exPtr = state.getException();
            m_unwrapState->setException(std::move(exPtr));
            return;
        }

        if constexpr (isFuture<T>) {
            using NextT = typename T::Type;
            using NextFutureCallaback = UnwrapperFutureCallback<NextT, UnwrappedT>;

            auto nextState = state.getValue().detachState();
            auto nextCallback = std::make_unique<NextFutureCallaback>(std::move(m_unwrapState));
            nextState->setCallback(std::move(nextCallback));
        } else if constexpr (!std::is_void_v<UnwrappedT>) {
            auto& value = state.getValue();
            m_unwrapState->setValue(std::move(value));
        } else {
            m_unwrapState->setValue();
        }
    }

private:
    RefPtr<detail::FutureState<UnwrappedT>> m_unwrapState;
};

template<typename T, typename Fn>
class FutureThenAOHandler final : public AOHandler
{
public:
    using NextFutureState = typename NextFutureState<T, Fn>::Type;

    template<typename F>
    FutureThenAOHandler(F&& f, RefPtr<FutureState<T>> futureState, RefPtr<NextFutureState> nextFutureState)
      : m_futureState(std::move(futureState))
      , m_nextFutureState(std::move(nextFutureState))
      , m_fn(std::forward<F>(f))
    {}

    void call() override
    {
        assert(m_futureState->hasResult());

        if (m_futureState->hasException()) {
            auto& exPtr = m_futureState->getException();
            m_nextFutureState->setException(std::move(exPtr));
            return;
        }

        if constexpr (std::is_void_v<T>) {
            m_nextFutureState->calcResult(std::move(m_fn));
        } else {
            auto& value = m_futureState->getValue();
            m_nextFutureState->calcResult(m_fn, std::move(value));
        }
    }

    void cancel() override
    {
        auto exPtr = std::make_exception_ptr(AsyncOperationWasCancelled());
        m_nextFutureState->setException(std::move(exPtr));
    }

private:
    RefPtr<FutureState<T>> m_futureState;
    RefPtr<NextFutureState> m_nextFutureState;
    Fn m_fn;
};

template<typename T, typename Fn>
class FutureFailAOHandler final : public AOHandler
{
public:
    template<typename F>
    FutureFailAOHandler(F&& f, RefPtr<FutureState<T>> futureState, RefPtr<FutureState<T>> nextFutureState)
      : m_futureState(std::move(futureState))
      , m_nextFutureState(std::move(nextFutureState))
      , m_fn(std::forward<F>(f))
    {}

    void call() override
    {
        assert(m_futureState->hasResult());

        if (m_futureState->hasException()) {
            auto& exPtr = m_futureState->getException();
            m_nextFutureState->calcResult(std::move(m_fn), std::move(exPtr));
            return;
        }

        if constexpr (std::is_void_v<T>) {
            m_nextFutureState->setValue();
        } else {
            auto& value = m_futureState->getValue();
            m_nextFutureState->setValue(std::move(value));
        }
    }

    void cancel() override
    {
        auto exPtr = std::make_exception_ptr(AsyncOperationWasCancelled());
        m_nextFutureState->setException(std::move(exPtr));
    }

private:
    RefPtr<FutureState<T>> m_futureState;
    RefPtr<FutureState<T>> m_nextFutureState;
    Fn m_fn;
};

}   // namespace detail
}   // namespace nhope
