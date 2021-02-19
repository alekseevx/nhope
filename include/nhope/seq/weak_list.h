#pragma once

#include <iterator>
#include <list>
#include <memory>
#include <mutex>

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

            m_pos++;
            m_current = findCur();

            while (m_current == nullptr && m_pos != m_list.end()) {
                m_pos++;
                m_current = findCur();
            }

            return *this;
        }

    private:
        std::shared_ptr<T> findCur() const
        {
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
        clearExpired();
        for (const auto& ptr : m_list) {
            fn(ptr.lock());
        }
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
    using ListIterator = typename List::iterator;

    struct TSWeakIterator
    {
        TSWeakIterator(LockableValue<List>& l, ListIterator pos)
          : m_ra(l.readAccess())
          , m_pos(pos)
        {}

        bool operator!=(const TSWeakIterator& o) const noexcept
        {
            return m_pos != o.m_pos;
        }

        std::shared_ptr<T> operator*() const noexcept
        {
            return *m_pos;
        }

        TSWeakIterator& operator++() noexcept
        {
            ++m_pos;
            return *this;
        }

    private:
        typename LockableValue<List>::ReadAccess m_ra;
        mutable ListIterator m_pos{};
    };

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
            fn(ptr);
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

    TSWeakIterator begin()
    {
        auto ra = m_locker.readAccess();
        return TSWeakIterator(m_locker, ra->begin());
    }

    TSWeakIterator end()
    {
        auto ra = m_locker.readAccess();
        return TSWeakIterator(m_locker, ra->end());
    }

private:
    List m_list;
    LockableValue<List> m_locker{m_list};
};

}   // namespace nhope