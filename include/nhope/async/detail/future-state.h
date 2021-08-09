#pragma once

#include <atomic>
#include <cassert>
#include <cstddef>
#include <exception>
#include <memory>
#include <type_traits>
#include <utility>

#include "nhope/async/ao-context.h"
#include "nhope/async/event.h"
#include "nhope/utils/detail/compiler.h"
#include "nhope/utils/detail/ref-ptr.h"

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

template<typename T>
class FutureState;

enum FutureFlag : std::size_t
{
    HasResult = 1 << 0,
    HasCallback = 1 << 1,
};

template<typename T>
class FutureCallback
{
public:
    virtual ~FutureCallback() = default;

    virtual void futureReady(FutureState<T>& state, FutureFlag trigger) = 0;
};

template<typename T>
class FutureResultStorage final
{
public:
    FutureResultStorage()
      : m_curVariant(ContentVariants::Nothing)
    {}

    ~FutureResultStorage()
    {
        if (m_curVariant == ContentVariants::Value) {
            if constexpr (!std::is_void_v<T>) {
                // NOLINTNEXTLINE(cppcoreguidelines-pro-type-union-access)
                m_value.~T();
            }
        } else if (m_curVariant == ContentVariants::Exception) {
            // NOLINTNEXTLINE(cppcoreguidelines-pro-type-union-access)
            m_exception.~exception_ptr();
        }
    }

    template<typename... Args>
    void setValue(Args&&... args)
    {
        assert(m_curVariant == ContentVariants::Nothing);
        if constexpr (!std::is_void_v<T>) {
            // NOLINTNEXTLINE(cppcoreguidelines-pro-type-union-access)
            new (std::addressof(m_value)) T(std::forward<Args>(args)...);
        }
        m_curVariant = ContentVariants::Value;
    }

    template<typename Tp = T, typename = std::enable_if_t<!std::is_void_v<Tp>>>
    Tp& value() noexcept
    {
        assert(m_curVariant == ContentVariants::Value);
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-union-access)
        return m_value;
    }

    template<typename... Args>
    void setException(Args&&... args)
    {
        assert(m_curVariant == ContentVariants::Nothing);
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-union-access)
        new (std::addressof(m_exception)) std::exception_ptr(std::forward<Args>(args)...);
        m_curVariant = ContentVariants::Exception;
    }

    [[nodiscard]] std::exception_ptr& exception() noexcept
    {
        assert(m_curVariant == ContentVariants::Exception);
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-union-access)
        return m_exception;
    }

    [[nodiscard]] bool hasException() const noexcept
    {
        return m_curVariant == ContentVariants::Exception;
    }

private:
    enum class ContentVariants : std::size_t
    {
        Nothing,
        Value,
        Exception,
    };

    class Void
    {};

    ContentVariants m_curVariant;
    union
    {
        std::conditional_t<!std::is_void_v<T>, T, Void> m_value;   // NOLINT(readability-identifier-naming)
        std::exception_ptr m_exception;                            // NOLINT(readability-identifier-naming)
    };
};

template<typename T>
class FutureState final
{
public:
    using Type = T;

    template<typename... Args>
    void setValue(Args&&... args)
    {
        assert(!this->hasResult());

        m_resultStorage.setValue(std::forward<Args>(args)...);
        this->setFlag(FutureFlag::HasResult);
    }

    T value()
    {
        assert(this->hasResult());

        if constexpr (!std::is_void_v<T>) {
            return std::move(m_resultStorage.value());
        }
    }

    void setException(std::exception_ptr exception)
    {
        assert(!this->hasResult());
        m_resultStorage.setException(std::move(exception));
        this->setFlag(FutureFlag::HasResult);
    }

    [[nodiscard]] bool hasException() const noexcept
    {
        assert(this->hasResult());
        return m_resultStorage.hasException();
    }

    std::exception_ptr& exception() noexcept
    {
        assert(this->hasResult());
        return m_resultStorage.exception();
    }

    template<typename Fn, typename... Args>
    void calcResult(Fn&& fn, Args&&... args)
    {
        assert(!this->hasResult());

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

    [[nodiscard]] bool hasResult() const noexcept
    {
        const int flags = m_flags.load(std::memory_order_acquire);
        return (flags & FutureFlag::HasResult) != 0;
    }

    void setCallback(std::unique_ptr<FutureCallback<T>> callback)
    {
        assert(!this->hasCallback());

        m_callback = std::move(callback);
        this->setFlag(FutureFlag::HasCallback);
    }

    [[nodiscard]] bool hasCallback() const noexcept
    {
        const int flags = m_flags.load(std::memory_order_acquire);
        return (flags & FutureFlag::HasCallback) != 0;
    }

private:
    void setFlag(FutureFlag flag)
    {
        assert((m_flags & flag) == 0);   //  The flag is not set yet.

        auto curFlags = m_flags.load(std::memory_order_acquire);
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
        m_callback->futureReady(*this, flag);
    }

    std::atomic<std::size_t> m_flags = 0;

    FutureResultStorage<T> m_resultStorage;
    std::unique_ptr<FutureCallback<T>> m_callback;
};

template<typename T>
class CallAOHandlerFutureCallback final : public FutureCallback<T>
{
public:
    CallAOHandlerFutureCallback(AOContext& aoCtx, std::unique_ptr<AOHandler> aoHandler)
      : m_callAOHandler(aoCtx.putAOHandler(std::move(aoHandler)))
    {}

    void futureReady(FutureState<T>& /*unused*/, FutureFlag trigger) override
    {
        if (trigger == FutureFlag::HasResult) {
            m_callAOHandler(Executor::ExecMode::ImmediatelyIfPossible);
        } else {
            /* This branch we exclude a situation where the 
                callback will be called from Future::then or Future::fail. */
            m_callAOHandler(Executor::ExecMode::AddInQueue);
        }
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

    void futureReady(FutureState<T>& /*unused*/, FutureFlag /*unused*/) override
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
    explicit UnwrapperFutureCallback(RefPtr<detail::FutureState<UnwrappedT>> unwrapState)
      : m_unwrapState(std::move(unwrapState))
    {}

    void futureReady(FutureState<T>& state, FutureFlag /*unused*/) override
    {
        if (state.hasException()) {
            m_unwrapState->setException(state.exception());
            return;
        }

        if constexpr (isFuture<T>) {
            using NextT = typename T::Type;
            using NextFutureCallaback = UnwrapperFutureCallback<NextT, UnwrappedT>;

            auto nextState = state.value().detachState();
            auto nextCallback = std::make_unique<NextFutureCallaback>(std::move(m_unwrapState));
            nextState->setCallback(std::move(nextCallback));
        } else if constexpr (!std::is_void_v<UnwrappedT>) {
            m_unwrapState->setValue(state.value());
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
            m_nextFutureState->setException(m_futureState->exception());
            return;
        }

        if constexpr (std::is_void_v<T>) {
            m_nextFutureState->calcResult(std::move(m_fn));
        } else {
            m_nextFutureState->calcResult(std::move(m_fn), m_futureState->value());
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
            m_nextFutureState->calcResult(std::move(m_fn), m_futureState->exception());
            return;
        }

        if constexpr (std::is_void_v<T>) {
            m_nextFutureState->setValue();
        } else {
            m_nextFutureState->setValue(m_futureState->value());
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
