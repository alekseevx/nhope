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
        List& list;
        mutable ListIterator pos{};
        std::shared_ptr<T> current;

        WeakIterator(List& l, ListIterator pos)
          : list(l)
          , pos(pos)
        {
            current = pos != list.end() ? pos->lock() : nullptr;
        }

        bool operator!=(const WeakIterator& o) const noexcept
        {
            return pos != o.pos && current != nullptr;
        }

        std::shared_ptr<T> operator*() const noexcept
        {
            return current;
        }

        WeakIterator& operator++() noexcept
        {
            if (pos == list.end()) {
                return *this;
            }

            pos++;
            current = findCur();

            while (current == nullptr && pos != list.end()) {
                pos++;
                current = findCur();
            }

            return *this;
        }

    private:
        std::shared_ptr<T> findCur() const
        {
            if (pos == list.end()) {
                return std::shared_ptr<T>();
            }
            auto ptr = pos->lock();
            return ptr;
        }
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
        typename LockableValue<List>::ReadAccess ra;
        mutable ListIterator pos{};

        TSWeakIterator(LockableValue<List>& l, ListIterator pos)
          : ra(l.readAccess())
          , pos(pos)
        {}

        bool operator!=(const TSWeakIterator& o) const noexcept
        {
            return pos != o.pos;
        }

        std::shared_ptr<T> operator*() const noexcept
        {
            return *pos;
        }

        TSWeakIterator& operator++() noexcept
        {
            ++pos;
            return *this;
        }
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