#pragma once

#include <atomic>
#include <cassert>
#include <cstddef>
#include <exception>
#include <memory>
#include <type_traits>
#include <utility>

#include "nhope/async/ao-context-error.h"
#include "nhope/async/ao-context.h"
#include "nhope/async/event.h"
#include "nhope/utils/detail/compiler.h"
#include "nhope/utils/detail/ref-ptr.h"
#include "nhope/async/detail/ts-shared-flag.h"

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

    virtual void futureReady(FutureState<T>* state, FutureFlag trigger) = 0;
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

    [[nodiscard]] bool hasValue() const noexcept
    {
        return m_curVariant == ContentVariants::Value;
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
class FutureState final : public BaseRefCounter
{
public:
    using Type = T;

    FutureState() = default;

    explicit FutureState(const SharedFlag& cancelFlag)
      : m_cancelled(cancelFlag)
    {}

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

    [[nodiscard]] bool hasValue() const noexcept
    {
        assert(this->hasResult());
        return m_resultStorage.hasValue();
    }

    std::exception_ptr& exception() noexcept
    {
        assert(this->hasResult());
        return m_resultStorage.exception();
    }

    template<typename Fn, typename... Args>
    void calcResult(Fn fn, Args&&... args)
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
        const auto flags = m_flags.load(std::memory_order_acquire);
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
        const auto flags = m_flags.load(std::memory_order_acquire);
        return (flags & FutureFlag::HasCallback) != 0;
    }

    [[nodiscard]] bool wasCancelled() const noexcept
    {
        return m_cancelled.isSet();
    }

    void cancel()
    {
        m_cancelled.set();
    }

    [[nodiscard]] SharedFlag shareCancelToken() const noexcept
    {
        return m_cancelled;
    }

    void setCancelToken(const SharedFlag& f)
    {
        m_cancelled = f;
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
        m_callback->futureReady(this, flag);
    }

    std::atomic<std::size_t> m_flags = 0;
    SharedFlag m_cancelled;

    FutureResultStorage<T> m_resultStorage;
    std::unique_ptr<FutureCallback<T>> m_callback;
};

template<typename T>
class SetEventFutureCallback final : public FutureCallback<T>
{
public:
    explicit SetEventFutureCallback(RefPtr<Event> event)
      : m_event(std::move(event))
    {}

    void futureReady(FutureState<T>* /*unused*/, FutureFlag /*unused*/) override
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

    void futureReady(FutureState<T>* state, FutureFlag /*unused*/) override
    {
        if (state->hasException()) {
            m_unwrapState->setException(state->exception());
            return;
        }

        if constexpr (isFuture<T>) {
            using NextT = typename T::Type;
            using NextFutureCallaback = UnwrapperFutureCallback<NextT, UnwrappedT>;

            auto nextState = state->value().detachState();
            nextState->setCancelToken(m_unwrapState->shareCancelToken());
            auto nextCallback = std::make_unique<NextFutureCallaback>(std::move(m_unwrapState));
            nextState->setCallback(std::move(nextCallback));
        } else if constexpr (!std::is_void_v<UnwrappedT>) {
            m_unwrapState->setValue(state->value());
        } else {
            m_unwrapState->setValue();
        }
    }

private:
    RefPtr<detail::FutureState<UnwrappedT>> m_unwrapState;
};

template<typename T, typename Fn>
class FutureThenCallback final : public FutureCallback<T>
{
public:
    using NextFutureState = typename NextFutureState<T, Fn>::Type;

    template<typename F>
    FutureThenCallback(F&& f, RefPtr<NextFutureState> nextFutureState)
      : m_nextFutureState(std::move(nextFutureState))
      , m_fn(std::forward<F>(f))
    {}

    void futureReady(FutureState<T>* state, FutureFlag /*unused*/) override
    {
        assert(m_nextFutureState != nullptr);
        auto nextFutureState = std::move(m_nextFutureState);

        if (state->hasException()) {
            nextFutureState->setException(state->exception());
            return;
        }

        if (nextFutureState->wasCancelled()) {
            nextFutureState->setException(std::make_exception_ptr(AsyncOperationWasCancelled()));
            return;
        }

        if constexpr (std::is_void_v<T>) {
            nextFutureState->calcResult(std::move(m_fn));
        } else {
            nextFutureState->calcResult(std::move(m_fn), state->value());
        }
    }

    void futureCancelled()
    {
        if (m_nextFutureState != nullptr) {
            auto exPtr = std::make_exception_ptr(AsyncOperationWasCancelled());
            m_nextFutureState->setException(std::move(exPtr));
            m_nextFutureState = nullptr;
        }
    }

private:
    RefPtr<NextFutureState> m_nextFutureState;
    Fn m_fn;
};

template<typename T, typename Fn>
class FutureFailCallback final : public FutureCallback<T>
{
public:
    using NextFutureState = FutureState<T>;

    template<typename F>
    FutureFailCallback(F&& f, RefPtr<FutureState<T>> nextFutureState)
      : m_nextFutureState(std::move(nextFutureState))
      , m_fn(std::forward<Fn>(f))
    {}

    void futureReady(FutureState<T>* state, FutureFlag /*unused*/) override
    {
        assert(m_nextFutureState != nullptr);
        auto nextFutureState = std::move(m_nextFutureState);

        if (state->hasValue()) {
            if constexpr (std::is_void_v<T>) {
                nextFutureState->setValue();
            } else {
                nextFutureState->setValue(state->value());
            }
            return;
        }

        nextFutureState->calcResult(std::move(m_fn), state->exception());
    }

    void futureCancelled()
    {
        if (m_nextFutureState != nullptr) {
            auto exPtr = std::make_exception_ptr(AsyncOperationWasCancelled());
            m_nextFutureState->setException(std::move(exPtr));
            m_nextFutureState = nullptr;
        }
    }

private:
    RefPtr<NextFutureState> m_nextFutureState;
    Fn m_fn;
};

template<typename T, typename FB>
class FutureCallbackWithAOContext
  : public FutureCallback<T>
  , public AOContextCloseHandler
{
public:
    template<typename... FBArgs>
    FutureCallbackWithAOContext(AOContext& ctx, FBArgs&&... fbArgs)
      : m_futureCallback(std::forward<FBArgs>(fbArgs)...)
      , m_aoCtxRef(ctx)
    {
        m_aoCtxRef.addCloseHandler(*this);
    }

    ~FutureCallbackWithAOContext() override
    {
        m_aoCtxRef.removeCloseHandler(*this);
    }

    void futureReady(FutureState<T>* state, FutureFlag trigger) override
    {
        using ExecMode = Executor::ExecMode;
        m_aoCtxRef.exec(
          [this, state = refPtrFromRawPtr(state), trigger] {
              m_futureCallback.futureReady(state.get(), trigger);
          },
          trigger == FutureFlag::HasResult ? ExecMode::ImmediatelyIfPossible : ExecMode::AddInQueue);
    }

    void aoContextClose() noexcept override
    {
        m_futureCallback.futureCancelled();
    }

private:
    FB m_futureCallback;
    AOContextRef m_aoCtxRef;
};

template<typename T, typename Fn>
using FutureThenWithAOCtxCallback = FutureCallbackWithAOContext<T, FutureThenCallback<T, Fn>>;

template<typename T, typename Fn>
using FutureFailWithAOCtxCallback = FutureCallbackWithAOContext<T, FutureFailCallback<T, Fn>>;

}   // namespace detail
}   // namespace nhope
