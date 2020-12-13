#pragma once

#include <chrono>
#include <exception>
#include <type_traits>
#include <utility>

#include <boost/chrono/duration.hpp>
#include <boost/thread/future.hpp>
#include <boost/thread/futures/future_status.hpp>

#include "nhope/utils/exception_ptr.h"

namespace nhope::asyncs {

using FutureStatus = boost::future_status;
using FutureState = boost::future_state::state;

template<typename R>
class Promise;

template<typename R>
class Future final
{
    friend Promise<R>;

    template<typename Rp, typename... Args>
    friend Future<Rp> makeReadyFuture(Args&&... args);

public:
    using Impl = boost::future<R>;

    Future() = default;
    Future(const Future&) = delete;
    Future(Future&&) noexcept = default;
    explicit Future(Impl&& impl) noexcept
      : m_impl(std::move(impl))
    {}

    Future& operator=(const Future&) = delete;
    Future& operator=(Future&&) noexcept = default;
    Future& operator=(Impl&& impl)
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

private:
    boost::future<R> m_impl;
};

template<typename R>
class Promise final
{
public:
    using Impl = boost::promise<R>;

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
    Impl m_impl;
};

template<>
class Promise<void> final
{
public:
    using Impl = boost::promise<void>;

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
    Impl m_impl;
};

template<typename R, typename... Args>
Future<R> makeReadyFuture(Args&&... args)
{
    return Future(boost::make_ready_future(std::forward<Args>(args)...));
}

}   // namespace nhope::asyncs
