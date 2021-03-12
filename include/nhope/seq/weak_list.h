#pragma once

#include <iterator>
#include <list>
#include <memory>
#include <functional>

#include "nhope/async/lockable-value.h"
#include "nhope/async/reverse_lock.h"

namespace nhope {

template<typename T>
class WeakList
{
    using List = std::list<std::weak_ptr<T>>;
    using ListIterator = typename List::iterator;

    struct WeakIterator
    {
        WeakIterator(List& l, ListIterator pos)
          : m_list(l)
          , m_pos(pos)
        {
            m_current = pos != m_list.end() ? pos->lock() : nullptr;
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
            if (m_pos == m_list.end()) {
                return *this;
            }

            m_current = next();

            while (m_current == nullptr && m_pos != m_list.end()) {
                m_current = next();
            }

            return *this;
        }

    private:
        std::shared_ptr<T> next() const
        {
            m_pos++;
            if (m_pos == m_list.end()) {
                return std::shared_ptr<T>();
            }
            auto ptr = m_pos->lock();
            return ptr;
        }

        List& m_list;
        mutable ListIterator m_pos{};
        std::shared_ptr<T> m_current;
    };

public:
    using iterator = WeakIterator;

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
                                       auto ptr = v.lock();
                                       if (ptr != nullptr) {
                                           return *ptr == val;
                                       }
                                       return false;
                                   });
            it != m_list.end()) {
            return it->lock();
        }
        return std::shared_ptr<T>();
    }

    std::shared_ptr<T> find_if(std::function<bool(const T&)> fn)
    {
        if (auto it = std::find_if(m_list.begin(), m_list.end(),
                                   [&](const auto& v) {
                                       if (auto ptr = v.lock(); ptr != nullptr) {
                                           return fn(*ptr);
                                       }
                                       return false;
                                   });
            it != m_list.end()) {
            return it->lock();
        }
        return std::shared_ptr<T>();
    }

    void clearExpired()
    {
        auto it = m_list.begin();
        while (it != m_list.end()) {
            auto ptr = it->lock();
            if (ptr != nullptr) {
                it++;
            } else {
                it = m_list.erase(it);
            }
        }
    }

    template<typename... Args>
    void emplace_back(Args&&... args)
    {
        m_list.emplace_back(std::forward<Args>(args)...);
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
        List copy;
        {
            auto ra = m_locker.readAccess();
            copy = *ra;
        }
        for (const auto& ptr : copy) {
            if (ptr != nullptr) {
                fn(ptr);
            }
        }
    }

    void clearExpired()
    {
        auto wa = m_locker.writeAccess();
        wa->clearExpired();
    }

    template<typename... Args>
    void emplace_back(Args&&... args)
    {
        auto wa = m_locker.writeAccess();
        wa->emplace_back(std::forward<Args>(args)...);
    }

    [[nodiscard]] size_t size() const noexcept
    {
        auto ra = m_locker.readAccess();
        return ra->size();
    }
    [[nodiscard]] bool empty() const noexcept
    {
        auto ra = m_locker.readAccess();
        return ra->empty();
    }

    template<typename V>
    std::shared_ptr<T> find(V&& val)
    {
        auto wa = m_locker.writeAccess();
        return wa->find(val);
    }

    std::shared_ptr<T> find_if(std::function<bool(const T&)> fn)
    {
        auto wa = m_locker.writeAccess();
        return wa->find_if(std::move(fn));
    }

private:
    LockableValue<List> m_locker;
};

}   // namespace nhope