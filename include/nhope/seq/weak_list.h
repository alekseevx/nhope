#pragma once

#include <iterator>
#include <list>
#include <memory>

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

}   // namespace nhope