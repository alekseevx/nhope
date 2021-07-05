#pragma once

#include <iterator>
#include <list>
#include <memory>
#include <functional>
#include <mutex>
#include <shared_mutex>

#include "nhope/async/lockable-value.h"
#include "nhope/async/reverse_lock.h"
#include "nhope/async/future.h"
#include "nhope/utils/noncopyable.h"

namespace nhope {

template<typename T>
class WeakList : public Noncopyable
{
    struct Impl
    {
        std::weak_ptr<T> weak;
        nhope::Promise<void> expirePromise;
    };

    using List = std::list<Impl>;
    using ListIterator = typename List::iterator;

    struct WeakIterator
    {
        WeakIterator(List& l, ListIterator pos)
          : m_list(l)
          , m_pos(pos)
        {
            m_current = pos != m_list.end() ? pos->weak.lock() : nullptr;
        }

        bool operator!=(const WeakIterator& o) const noexcept
        {
            return m_pos != o.m_pos && m_current != nullptr;
        }

        std::shared_ptr<T> operator*() const noexcept
        {
            return m_current;
        }

        WeakIterator& operator++() noexcept
        {
            m_current = next();

            while (m_current == nullptr && m_pos != m_list.end()) {
                m_current = next();
            }

            return *this;
        }

    private:
        std::shared_ptr<T> next() const
        {
            ++m_pos;
            if (m_pos == m_list.end()) {
                return std::shared_ptr<T>();
            }
            auto ptr = m_pos->weak.lock();
            return ptr;
        }

        List& m_list;
        mutable ListIterator m_pos{};
        std::shared_ptr<T> m_current;
    };

public:
    using iterator = WeakIterator;

    ~WeakList()
    {
        clearExpired();
    }

    template<typename Fn>
    void forEach(Fn fn)
    {
        for (const auto& ptr : m_list) {
            if (auto pre = ptr.lock(); pre != nullptr) {
                fn(pre);
            }
        }
    }

    template<typename V>
    std::shared_ptr<T> find(V&& val)
    {
        if (auto it = std::find_if(m_list.begin(), m_list.end(),
                                   [&](const auto& v) {
                                       auto ptr = v.weak.lock();
                                       if (ptr != nullptr) {
                                           return *ptr == val;
                                       }
                                       return false;
                                   });
            it != m_list.end()) {
            return it->weak.lock();
        }
        return std::shared_ptr<T>();
    }

    std::shared_ptr<T> find_if(std::function<bool(const T&)> fn)
    {
        if (auto it = std::find_if(m_list.begin(), m_list.end(),
                                   [&](const auto& v) {
                                       if (auto ptr = v.weak.lock(); ptr != nullptr) {
                                           return fn(*ptr);
                                       }
                                       return false;
                                   });
            it != m_list.end()) {
            return it->weak.lock();
        }
        return std::shared_ptr<T>();
    }

    void clearExpired()
    {
        auto it = m_list.begin();
        while (it != m_list.end()) {
            auto ptr = it->weak.lock();
            if (ptr != nullptr) {
                it++;
            } else {
                it->expirePromise.setValue();
                it = m_list.erase(it);
            }
        }
    }

    Future<void> emplace_back(const std::weak_ptr<T>& weak)
    {
        Impl impl;
        auto f = impl.expirePromise.future();
        impl.weak = weak;
        m_list.emplace_back(std::move(impl));
        return f;
    }

    [[nodiscard]] size_t size() const noexcept
    {
        return m_list.size();
    }
    [[nodiscard]] bool empty() const noexcept
    {
        return m_list.empty();
    }

    WeakIterator begin() const
    {
        return WeakIterator(m_list, m_list.begin());
    }

    WeakIterator end() const
    {
        return WeakIterator(m_list, m_list.end());
    }

private:
    mutable List m_list;
};

template<typename T>
class TSWeakList
{
    using List = WeakList<T>;

public:
    template<typename Fn>
    void forEach(Fn fn)
    {
        clearExpired();
        std::shared_lock lock(m_mutex);
        for (const auto& ptr : m_list) {
            if (ptr != nullptr) {
                ReverseLock unlock(lock);
                fn(ptr);
            }
        }
    }

    void clearExpired()
    {
        std::scoped_lock<std::shared_mutex> lock(m_mutex);
        m_list.clearExpired();
    }

    Future<void> emplace_back(const std::weak_ptr<T>& weak)
    {
        std::scoped_lock lock(m_mutex);
        return m_list.emplace_back(weak);
    }

    [[nodiscard]] size_t size() const noexcept
    {
        std::shared_lock lock(m_mutex);
        return m_list.size();
    }
    [[nodiscard]] bool empty() const noexcept
    {
        std::shared_lock lock(m_mutex);
        return m_list.empty();
    }

    template<typename V>
    std::shared_ptr<T> find(V&& val)
    {
        std::scoped_lock lock(m_mutex);
        return m_list.find(val);
    }

    std::shared_ptr<T> find_if(std::function<bool(const T&)> fn)
    {
        std::scoped_lock lock(m_mutex);
        return m_list.find_if(std::move(fn));
    }

private:
    List m_list;
    mutable std::shared_mutex m_mutex;
};

}   // namespace nhope
