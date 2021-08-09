#pragma once

#include <cstddef>
#include <utility>
#include "nhope/utils/stack-storage.h"

namespace nhope {

template<typename K>
class StackSet final
{
public:
    class Item final
    {
    public:
        template<typename Kp>
        explicit Item(Kp key)
          : m_record(std::forward<Kp>(key), nullptr)
        {}

    private:
        typename StackStorage<K, std::nullptr_t>::Record m_record;
    };

    static bool contains(const K& key) noexcept
    {
        return StackStorage<K, std::nullptr_t>::contains(key);
    }
};

}   // namespace nhope
