#pragma once

#include <chrono>
#include <exception>
#include <functional>
#include <memory>
#include <type_traits>
#include <utility>

#include <boost/chrono/duration.hpp>
#include <boost/thread/future.hpp>
#include <boost/thread/futures/future_status.hpp>
#include <boost/thread/futures/launch.hpp>

#include "nhope/async/ao-context.h"
#include "nhope/utils/exception_ptr.h"

namespace nhope {

using FutureStatus = boost::future_status;
using FutureState = boost::future_state::state;
using Launch = boost::launch;

template<typename R>
class Promise;

template<typename R>
class Future;

template<typename T>
inline constexpr bool isFuture = false;

template<typename R>
inline constexpr bool isFuture<Future<R>> = true;

template<typename T>
inline constexpr bool isBoostFuture = false;

template<typename R>
inline constexpr bool isBoostFuture<boost::future<R>> = true;

template<typename T>
struct FutureResult
{};

template<typename R>
struct FutureResult<Future<R>>
{
    using Type = R;
};

template<typename R>
struct FutureResult<boost::future<R>>
{
    using Type = R;
};

template<typename R>
class Future final
{
    friend Promise<R>;

    template<typename Rp, typename... Args>
    friend Future<Rp> makeReadyFuture(Args&&... args);

    template<typename>
    friend class Future;

public:
    Future() = default;
    Future(const Future&) = delete;
    Future(Future&& other) noexcept = default;
    explicit Future(boost::future<R>&& impl) noexcept
      : m_impl(std::move(impl))
    {}

    Future& operator=(const Future&) = delete;
    Future& operator=(Future&&) noexcept = default;
    Future& operator=(boost::future<R>&& impl)
    {
        m_impl = std::move(impl);
        return *this;
    }

    [[nodiscard]] FutureState state() const
    {
        return m_impl.get_state();
    }

    [[nodiscard]] FutureStatus waitFor(const std::chrono::nanoseconds& timeout) const
    {
        const auto t = boost::chrono::nanoseconds(timeout.count());
        return m_impl.wait_for(t);
    }

    [[nodiscard]] bool valid() const
    {
        return m_impl.valid();
    }

    [[nodiscard]] bool isReady() const
    {
        return m_impl.is_ready();
    }

    void wait() const
    {
        m_impl.wait();
    }

    [[nodiscard]] bool hasValue() const
    {
        return m_impl.has_value();
    }

    R get()
    {
        return m_impl.get();
    }

    [[nodiscard]] bool hasException() const
    {
        return m_impl.has_exception();
    }

    [[nodiscard]] std::exception_ptr exception() const
    {
        auto ex = m_impl.get_exception_ptr();
        return utils::toStdExceptionPtr(ex);
    }

    template<typename Fn>
    auto then(Fn&& fn)
    {
        return this->then(m_impl.launch_policy(), std::forward<Fn>(fn));
    }

    template<typename Fn>
    auto then(Launch launch, Fn&& fn)
    {
        auto boostFuture = m_impl.then(launch, [fn](boost::future<R> boostFuture) mutable {
            return callThenHandler(std::move(fn), std::move(boostFuture));
        });

        return fromBoostFuture(std::move(boostFuture));
    }

    template<typename Fn>
    auto then(AOContext& aoCtx, Fn&& fn)
    {
        return this->thenForAOCtx(aoCtx, [fn](boost::future<R> finishedFuture) mutable {
            return callThenHandler(std::move(fn), std::move(finishedFuture));
        });
    }

    template<typename Fn>
    Future<R> fail(Fn&& fn)
    {
        return this->fail(m_impl.launch_policy(), std::forward<Fn>(fn));
    }

    template<typename Fn>
    Future<R> fail(Launch launch, Fn&& fn)
    {
        auto boostFuture = m_impl.then(launch, [fn](boost::future<R> finishedFuture) mutable {
            return callErrorHandler(std::move(fn), std::move(finishedFuture));
        });

        return fromBoostFuture(std::move(boostFuture));
    }

    template<typename Fn>
    Future<R> fail(AOContext& aoCtx, Fn&& fn)
    {
        return this->thenForAOCtx(aoCtx, [fn](boost::future<R> finishedFuture) mutable {
            return callErrorHandler(std::move(fn), std::move(finishedFuture));
        });
    }

private:
    template<typename Then>
    auto thenForAOCtx(AOContext& aoCtx, Then&& then)
    {
        using ResultOfThen = std::invoke_result_t<Then, boost::future<R>>;

        auto boostPromise = std::make_shared<boost::promise<ResultOfThen>>();
        auto boostFuture = boostPromise->get_future();

        std::function thenWrapper = [then, boostPromise](std::shared_ptr<boost::future<R>> boostFuturePtr) mutable {
            try {
                if constexpr (std::is_void_v<ResultOfThen>) {
                    then(std::move(*boostFuturePtr));
                    boostPromise->set_value();
                } else {
                    boostPromise->set_value(then(std::move(*boostFuturePtr)));
                }
            } catch (...) {
                auto exPtr = boost::current_exception();
                boostPromise->set_exception(exPtr);
            }
        };
        std::function cancel = [boostPromise] {
            auto exPtr = std::make_exception_ptr(AsyncOperationWasCancelled());
            boostPromise->set_exception(utils::toBoostExceptionPtr(exPtr));
        };
        std::function safeThenWrapper = aoCtx.newAsyncOperation(std::move(thenWrapper), std::move(cancel));

        m_impl.then([safeThenWrapper](boost::future<R> boostFuture) {
            auto boostFuturePtr = std::make_shared<boost::future<R>>(std::move(boostFuture));
            safeThenWrapper(boostFuturePtr);
        });

        return fromBoostFuture(std::move(boostFuture));
    }

    template<typename Rp>
    static auto unwrapBoostFuture(boost::future<Rp>&& future)
    {
        if constexpr (isBoostFuture<Rp>) {
            return unwrap(future.unwrap());
        } else {
            return std::move(future);
        }
    }

    template<typename Rp>
    static auto unwrapFuture(Future<Rp>&& future)
    {
        return unwrapBoostFuture(std::move(future.m_impl));
    }

    template<typename T>
    static auto unwrap(T&& value)
    {
        if constexpr (isBoostFuture<T>) {
            return unwrapBoostFuture(std::move(value));
        } else if constexpr (isFuture<T>) {
            return unwrapFuture(std::move(value));
        } else {
            return std::move(value);
        }
    }

    template<typename Rp>
    static auto fromBoostFuture(boost::future<Rp> boostFuture)
    {
        auto unwrappedBoostFuture = unwrapBoostFuture(std::move(boostFuture));

        using UnwrappedBoostFuture = decltype(unwrappedBoostFuture);
        using UnwrappedResult = typename FutureResult<UnwrappedBoostFuture>::Type;
        return Future<UnwrappedResult>(std::move(unwrappedBoostFuture));
    }

    template<typename Fn>
    static auto callThenHandler(Fn&& fn, boost::future<R> finishedFuture)
    {
        if constexpr (std::is_void_v<R>) {
            using ResultOfFn = std::invoke_result_t<Fn>;

            if constexpr (std::is_void_v<ResultOfFn>) {
                finishedFuture.get();
                fn();
                return;
            } else {
                finishedFuture.get();
                return unwrap(fn());
            }
        } else {
            using ResultOfFn = std::invoke_result_t<Fn, R>;

            if constexpr (std::is_void_v<ResultOfFn>) {
                fn(finishedFuture.get());
                return;
            } else {
                return unwrap(fn(finishedFuture.get()));
            }
        }
    }

    template<typename Fn>
    static R callErrorHandler(Fn&& fn, boost::future<R> finishedFuture)
    {
        if (!finishedFuture.has_exception()) {
            return finishedFuture.get();
        }

        auto exPtr = finishedFuture.get_exception_ptr();
        return fn(utils::toStdExceptionPtr(exPtr));
    }

private:
    boost::future<R> m_impl;
};

template<typename R>
class Promise final
{
    friend class Future<R>;

public:
    Promise() = default;
    Promise(Promise&&) noexcept = default;

    Promise& operator=(Promise&&) noexcept = default;

    void setValue(R&& r)
    {
        m_impl.set_value(std::forward<R>(r));
    }

    void setValue(const R& r)
    {
        m_impl.set_value(r);
    }

    void setException(const std::exception_ptr& ex)
    {
        m_impl.set_exception(utils::toBoostExceptionPtr(ex));
    }

    Future<R> future()
    {
        return Future<R>(m_impl.get_future());
    }

private:
    boost::promise<R> m_impl;
};

template<>
class Promise<void> final
{
    friend class Future<void>;

public:
    Promise() = default;
    Promise(Promise&&) noexcept = default;

    Promise& operator=(Promise&&) noexcept = default;

    void setValue()
    {
        m_impl.set_value();
    }

    void setException(const std::exception_ptr& ex)
    {
        m_impl.set_exception(utils::toBoostExceptionPtr(ex));
    }

    Future<void> future()
    {
        return Future<void>(m_impl.get_future());
    }

private:
    boost::promise<void> m_impl;
};

template<typename R>
Future<R> makeReadyFuture(R&& value)
{
    return Future<R>(boost::make_ready_future<R>(std::forward<R>(value)));
}

inline Future<void> makeReadyFuture()
{
    return Future<void>(boost::make_ready_future());
}

template<typename R, typename Fn, typename... Args>
Future<R> toThread(Fn&& fn, Args&&... args)
{
    return Future<R>(boost::async(boost::launch::async, std::forward<Fn>(fn), std::forward<Args>(args)...));
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

}   // namespace nhope
