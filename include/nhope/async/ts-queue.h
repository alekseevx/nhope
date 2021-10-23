#pragma once

#include <cassert>

#include <condition_variable>
#include <limits>
#include <list>
#include <mutex>
#include <optional>
#include <utility>

namespace nhope {

template<typename T>
class TSQueue final
{
public:
    TSQueue(const TSQueue&) = delete;
    TSQueue& operator=(const TSQueue&) = delete;

    explicit TSQueue(std::size_t capacity = std::numeric_limits<std::size_t>::max())
      : m_capacity(capacity)
    {
        assert(capacity > 0);
    }

    void close()
    {
        std::scoped_lock lock(m_mutex);
        m_closed = true;
        m_rcv.notify_all();
        m_wcv.notify_all();
    }

    template<typename Tv>
    bool write(Tv&& value)
    {
        std::unique_lock lock(m_mutex);
        m_wcv.wait(lock, [this] {
            return m_closed || m_values.size() < m_capacity;
        });

        if (m_closed) {
            return false;
        }

        m_values.emplace_back(std::forward<Tv>(value));
        m_rcv.notify_one();
        return true;
    }

    template<typename Tv>
    bool write(Tv&& value, std::chrono::nanoseconds timeout)
    {
        std::unique_lock lock(m_mutex);

        const bool waitSuccess = m_wcv.wait_for(lock, timeout, [this] {
            return m_closed || m_values.size() < m_capacity;
        });

        if (!waitSuccess || m_closed) {
            return false;
        }

        m_values.emplace_back(std::forward<Tv>(value));
        m_rcv.notify_one();
        return true;
    }

    bool read(T& value)
    {
        std::unique_lock lock(m_mutex);

        m_rcv.wait(lock, [this] {
            return m_closed || !m_values.empty();
        });

        if (m_values.empty()) {
            return false;
        }

        value = std::move(m_values.front());
        m_values.pop_front();
        m_wcv.notify_one();
        return true;
    }

    std::optional<T> read()
    {
        std::unique_lock lock(m_mutex);
        m_rcv.wait(lock, [this] {
            return m_closed || !m_values.empty();
        });

        if (m_values.empty()) {
            return std::nullopt;
        }

        std::optional retval = std::move(m_values.front());
        m_values.pop_front();
        m_wcv.notify_one();
        return retval;
    }

    bool read(T& value, std::chrono::nanoseconds timeout)
    {
        std::unique_lock lock(m_mutex);

        const bool waitSuccess = m_rcv.wait_for(lock, timeout, [this] {
            return m_closed || !m_values.empty();
        });

        if (!waitSuccess || m_values.empty()) {
            return false;
        }

        value = std::move(m_values.front());
        m_values.pop_front();
        m_wcv.notify_one();
        return true;
    }

    [[nodiscard]] std::size_t size() const
    {
        std::unique_lock lock(m_mutex);
        return m_values.size();
    }

    [[nodiscard]] bool empty() const
    {
        std::unique_lock lock(m_mutex);
        return m_values.empty();
    }

private:
    const std::size_t m_capacity;

    mutable std::mutex m_mutex;
    std::condition_variable m_rcv;
    std::condition_variable m_wcv;

    bool m_closed = false;
    std::list<T> m_values;
};

}   // namespace nhope
