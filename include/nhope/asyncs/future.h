#pragma once

#include <chrono>
#include <exception>
#include <functional>
#include <type_traits>
#include <utility>

#include <boost/chrono/duration.hpp>
#include <boost/thread/future.hpp>
#include <boost/thread/futures/future_status.hpp>
#include <boost/thread/futures/launch.hpp>

#include "nhope/asyncs/ao-context.h"
#include "nhope/utils/exception_ptr.h"

namespace nhope::asyncs {

using FutureStatus = boost::future_status;
using FutureState = boost::future_state::state;

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

    [[nodiscard]] R get()
    {
        if constexpr (std::is_void_v<R>) {
            m_impl.get();
            return;
        } else {
            return m_impl.get();
        }
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
    auto thenValue(Fn&& fn)
    {
        auto boostFuture = m_impl.then([fn = std::move(fn)](boost::future<R> boostFuture) {
            if constexpr (std::is_void_v<R>) {
                using ResultOfFn = std::invoke_result_t<Fn>;

                if constexpr (std::is_void_v<ResultOfFn>) {
                    boostFuture.get();
                    fn();
                    return;
                } else {
                    boostFuture.get();
                    return unwrap(fn());
                }
            } else {
                using ResultOfFn = std::invoke_result_t<Fn, R>;

                if constexpr (std::is_void_v<ResultOfFn>) {
                    fn(boostFuture.get());
                    return;
                } else {
                    return unwrap(fn(boostFuture.get()));
                }
            }
        });

        auto unwrappedBoostFuture = unwrap(std::move(boostFuture));

        using UnwrappedBoostFuture = decltype(unwrappedBoostFuture);
        using Result = typename FutureResult<UnwrappedBoostFuture>::Type;

        return Future<Result>(std::move(unwrappedBoostFuture));
    }

    template<typename Fn>
    auto thenValue(AOContext& aoCtx, Fn&& fn)
    {
        return this->thenValue(aoCtx.newAsyncOperation(std::function(fn), nullptr));
    }

    template<typename Fn>
    auto thenException(Fn&& fn)
    {
        auto boostFuture = m_impl.then([fn = std::move(fn)](boost::future<R> boostFuture) {
            if (!boostFuture.has_exception()) {
                return boostFuture.get();
            }

            return fn(utils::toStdExceptionPtr(boostFuture.get_exception_ptr()));
        });

        auto unwrappedBoostFuture = unwrap(std::move(boostFuture));
        return Future<R>(std::move(unwrappedBoostFuture));
    }

    template<typename Fn>
    auto thenException(AOContext& aoCtx, Fn&& fn)
    {
        return this->thenValue(aoCtx.newAsyncOperation(std::function(fn), nullptr));
    }

private:
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

private:
    boost::future<R> m_impl;
};

template<typename R>
class Promise final
{
public:
    Promise() = default;
    Promise(Promise&&) noexcept = default;

    Promise& operator=(Promise&&) noexcept = default;

    void setValue(R&& r)
    {
        m_impl.set_value(std::forward<R>(r));
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

template<typename R, typename... Args>
Future<R> makeReadyFuture(Args&&... args)
{
    return Future(boost::make_ready_future(std::forward<Args>(args)...));
}

template<typename R, typename Fn, typename... Args>
Future<R> toThread(Fn&& fn, Args&&... args)
{
    return Future<R>(boost::async(boost::launch::async, std::forward<Fn>(fn), std::forward<Args>(args)...));
}

}   // namespace nhope::asyncs
