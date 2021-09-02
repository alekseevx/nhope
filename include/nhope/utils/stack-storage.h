#pragma once

#include <cstddef>
#include <utility>
#include "nhope/utils/noncopyable.h"

namespace nhope {

/**
 * @brief Provides access to data located on the stack of the calling function.
 *
 * Usage example:
 * @code
 * void a() {
 *    StackStorage<int, int>::Record record(10, 100);
 *    b();
 * }
 *
 * void b() {
 *     int* ptr = StackStorage<int, int>::get(10);
 *     assert(*ptr, 100);
 * }
 * @endcode
 * 
 * @tparam K key type
 * @tparam V value type
 */
template<typename K, typename V>
class StackStorage final
{
public:
    using Key = K;
    using Value = V;

    class Record final : Noncopyable
    {
        friend class StackStorage;

    public:
        template<typename Kp, typename Vp>
        Record(Kp&& key, Vp&& value)
          : m_key(std::forward<Kp>(key))
          , m_value(std::forward<Vp>(value))
          , m_pred(StackStorage::first)
        {
            StackStorage::first = this;
        }

        ~Record()
        {
            StackStorage::first = m_pred;
        }

        Value& value() noexcept
        {
            return m_value;
        }

    private:
        const Key m_key;
        Value m_value;

        Record* m_pred;
    };

    static Value* get(const Key& key) noexcept
    {
        for (Record* cur = first; cur != nullptr; cur = cur->m_pred) {
            if (key == cur->m_key) {
                return &cur->m_value;
            }
        }
        return nullptr;
    }

    static bool contains(const Key& key) noexcept
    {
        return StackStorage::get(key) != nullptr;
    }

    static std::size_t count(const Key& key) noexcept
    {
        std::size_t result = 0;
        for (Record* cur = first; cur != nullptr; cur = cur->m_pred) {
            if (key == cur->m_key) {
                ++result;
            }
        }
        return result;
    }

private:
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
    static thread_local Record* first;
};

template<typename K, typename V>
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
thread_local typename StackStorage<K, V>::Record* StackStorage<K, V>::first = nullptr;

}   // namespace nhope
