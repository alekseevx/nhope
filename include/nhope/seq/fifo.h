#pragma once

#include <array>
#include <cassert>
#include <cstddef>
#include <cstring>
#include <optional>
#include <span>
#include <type_traits>
#include <utility>

namespace nhope {

template<typename T, std::size_t Size>
class Fifo
{
    static_assert(std::is_standard_layout_v<T> && std::is_trivial_v<T>);

    template<std::size_t Value>
    static constexpr std::size_t nexPowerOf2()
    {
        static_assert(Value > 0);
        std::size_t v = Value;
        int power = 2;
        --v;
        while ((v >>= 1) != 0U) {
            power <<= 1;
        }
        return power;
    }

public:
    [[nodiscard]] std::size_t size() const noexcept
    {
        return m_count;
    }

    [[nodiscard]] bool empty() const noexcept
    {
        return m_count == 0;
    }

    void clear()
    {
        m_count = m_head = m_tail = 0;
    }

    std::size_t push(std::span<const T> data)
    {
        const auto freeSpace = Size - m_count;
        const auto count = std::min(data.size(), freeSpace);
        if (count == 0) [[unlikely]] {
            return 0;
        }

        if (m_head + count <= capacity) [[likely]] {
            copy(std::span(m_buffer).subspan(m_head, count), data.first(count));
            m_head = (m_head + count) & (capacity - 1);
        } else {
            const auto firstPartCount = capacity - m_head;
            auto src = std::span(m_buffer);
            copy(src.subspan(m_head), data.first(firstPartCount));
            m_head = (m_head + count) & (capacity - 1);
            copy(src.first(m_head), data.subspan(firstPartCount));
        }
        m_count += count;
        return count;
    }

    std::size_t push(T&& value)
    {
        return push(std::span<T, 1>(&value, 1));
    }

    /*!
     * @brief pop from fifo to data
     * 
     * @param data 
     * @return size_t really popped size from fifo
     */
    std::size_t pop(std::span<T> data)
    {
        const std::size_t count = std::min(data.size(), m_count);
        if (count == 0) [[unlikely]] {
            return count;
        }

        if (m_tail + count <= capacity) [[likely]] {
            copy(data, std::span(m_buffer).subspan(m_tail, count));
        } else {
            const auto firstPart = std::span(m_buffer).subspan(m_tail);
            const auto firstPartSize = firstPart.size();
            copy(data, firstPart);
            copy(std::span(data).subspan(firstPartSize), std::span(m_buffer).first(count - firstPartSize));
        }
        m_count -= count;
        m_tail = (m_tail + count) & (capacity - 1);

        return count;
    }

    [[nodiscard]] std::optional<T> pop()
    {
        T val{};
        if (pop(std::span<T, 1>(&val, 1)) == 0) {
            return std::nullopt;
        }
        return val;
    }

private:
    static void copy(std::span<T> dst, std::span<const T> src)
    {
        std::memcpy(dst.data(), src.data(), src.size() * sizeof(T));
    }
    static constexpr std::size_t capacity = nexPowerOf2<Size>();
    std::array<T, capacity> m_buffer{};
    std::size_t m_head{};
    std::size_t m_tail{};
    std::size_t m_count{};
};
}   // namespace nhope
