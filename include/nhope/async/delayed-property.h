#pragma once

#include <condition_variable>
#include <exception>
#include <mutex>
#include <chrono>

#include "nhope/async/future.h"
#include "nhope/async/lockable-value.h"
#include <nhope/async/ao-context.h>
#include "nhope/async/reverse_lock.h"

namespace nhope {
using namespace std::string_view_literals;

// Потокобезопасное свойство с возможностью отложенной установки
// Свойство будет применено только после вызова метода applyNewValue
template<typename T>
class DelayedProperty final
{
public:
    explicit DelayedProperty(const T& value) noexcept(std::is_nothrow_copy_constructible_v<T>)
      : m_value(value)
    {}

    explicit DelayedProperty(T&& value) noexcept(std::is_nothrow_move_constructible_v<T>)
      : m_value(std::move(value))
    {}

    template<typename... Args>
    explicit DelayedProperty(Args&&... args)
      : m_value(std::forward<Args>(args)...)
    {}

    template<typename V>
    Future<void> setNewValue(V&& value)
    {
        std::scoped_lock lock(m_mutex);

        if (m_promise.has_value()) {
            auto ex = std::make_exception_ptr(AsyncOperationWasCancelled("previous value was ignored"sv));
            m_promise.value().setException(ex);
        }

        m_promise = Promise<void>();
        m_newValue = std::forward<V>(value);

        m_newValCw.notify_all();

        return m_promise.value().future();
    }

    [[nodiscard]] bool hasNewValue() const noexcept
    {
        std::scoped_lock lock(m_mutex);
        return m_promise.has_value();
    }

    void waitNewValue()
    {
        std::unique_lock lock(m_mutex);
        m_newValCw.wait(lock, [this] {
            return m_promise.has_value();
        });
    }

    bool waitNewValue(std::chrono::milliseconds timeout)
    {
        std::unique_lock lock(m_mutex);
        return m_newValCw.wait_for(lock, timeout, [this] {
            return m_promise.has_value();
        });
    }

    template<typename Fn>
    void applyNewValue(Fn applyHandler)
    {
        std::unique_lock lock(m_mutex);
        if (!m_promise.has_value()) {
            return;
        }
        assert(m_promise.has_value());   // NOLINT

        auto newVal = m_newValue.value();
        auto promise = std::move(m_promise.value());
        m_newValue = std::nullopt;
        m_promise = std::nullopt;

        try {
            {
                ReverseLock unlock(lock);
                applyHandler(newVal);
            }
            m_value = newVal;
            promise.setValue();
        } catch (...) {
            promise.setException(std::current_exception());
        }
    }

    void applyNewValue()
    {
        constexpr auto nullhandler = [](const auto& /*unused*/) {
        };
        applyNewValue(nullhandler);
    }

    T getCurrentValue() const
    {
        std::scoped_lock lock(m_mutex);
        return m_value;
    }

private:
    T m_value;

    std::optional<T> m_newValue{std::nullopt};
    std::optional<Promise<void>> m_promise{std::nullopt};

    mutable std::mutex m_mutex;
    std::condition_variable m_newValCw;
};

}   // namespace nhope