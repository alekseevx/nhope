#pragma once

#include "nhope/async/future.h"
#include "nhope/async/lockable-value.h"
#include "nhope/async/reverse_lock.h"
#include <exception>
#include <mutex>

namespace nhope {

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

    Future<T> setNewValue(T&& value)
    {
        std::unique_lock lock(m_mutex);

        if (m_promise.has_value()) {
            auto ex = std::make_exception_ptr(std::runtime_error("previous value was ignored"));
            m_promise.value().setException(ex);
            m_promise = std::nullopt;
        }

        m_promise = Promise<T>();
        m_newValue = std::move(value);

        m_newValCw.notify_all();

        return m_promise.value().future();
    }

    [[nodiscard]] bool hasNewValue() const noexcept
    {
        std::unique_lock lock(m_mutex);
        return m_promise.has_value();
    }

    void applyNewValue(std::function<void(const T&)>&& applyHandler = nullptr)
    {
        while (!hasNewValue()) {
            std::unique_lock lock(m_mutex);
            m_newValCw.wait(lock);
        }
        std::unique_lock lock(m_mutex);

        assert(m_promise.has_value());   // NOLINT

        auto newVal = m_newValue.value();
        auto promise = std::move(m_promise.value());
        m_newValue = std::nullopt;
        m_promise = std::nullopt;

        try {
            if (applyHandler != nullptr) {
                ReverseLock unlock(lock);
                applyHandler(newVal);
            }
            m_value = newVal;
            promise.setValue(newVal);
        } catch (...) {
            promise.setException(std::current_exception());
        }
    }

    T getCurValue() const
    {
        std::unique_lock lock(m_mutex);
        return m_value;
    }

private:
    T m_value;

    std::optional<T> m_newValue{std::nullopt};
    std::optional<Promise<T>> m_promise{std::nullopt};

    mutable std::mutex m_mutex;
    std::condition_variable m_newValCw;
};

}   // namespace nhope