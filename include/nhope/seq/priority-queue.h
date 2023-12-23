#pragma once

#include <algorithm>
#include <cstddef>
#include <list>
#include <optional>
#include <utility>

#include "nhope/utils/type.h"

namespace nhope {

template<typename T>
class PriorityQueue
{
public:
    template<typename V>
    void push(V&& value, int priority = 0)
    {
        Storage val{priority, std::forward<V>(value)};
        const auto it =
          std::lower_bound(m_queue.begin(), m_queue.end(), val, [&](const Storage& lhs, const Storage& rhs) {
              return lhs.first < rhs.first;
          });
        m_queue.emplace(it, std::move(val));
    }

    template<typename Fn>
    void remove_if(Fn fn)
    {
        static_assert(checkFunctionSignatureV<Fn, bool, const T&, int>, "expect bool(const T&, int) signature");

        const auto end = m_queue.end();
        auto it = m_queue.begin();
        while (it != end) {
            if (fn(it->second, it->first)) {
                it = m_queue.erase(it);
                continue;
            }
            it++;
        }
    }

    void clear()
    {
        m_queue.clear();
    }

    std::optional<T> pop()
    {
        std::optional<T> result = std::nullopt;

        if (m_queue.empty()) {
            return result;
        }
        result = std::move(m_queue.back().second);
        m_queue.pop_back();
        return result;
    }

    [[nodiscard]] std::size_t size() const noexcept
    {
        return m_queue.size();
    }

    [[nodiscard]] bool empty() const noexcept
    {
        return m_queue.empty();
    }

private:
    using Storage = std::pair<int, T>;
    std::list<Storage> m_queue;
};

}   // namespace nhope
