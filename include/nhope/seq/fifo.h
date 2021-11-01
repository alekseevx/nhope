#pragma once

#include <array>
#include <cassert>
#include <cstddef>
#include <cstring>
#include <optional>
#include <type_traits>
#include <utility>

#include <gsl/span>

namespace nhope {

template<typename T, std::size_t Size>
class Fifo
{
    static_assert(std::is_standard_layout_v<T> && std::is_trivial_v<T>);

public:
    [[nodiscard]] size_t size() const noexcept
    {
        return m_offset;
    }

    [[nodiscard]] bool empty() const noexcept
    {
        return size() == 0;
    }

    void clear()
    {
        m_offset = 0;
    }

    size_t push(gsl::span<const T> data)
    {
        const auto inputDataSize = data.size();
        const auto freeSpace = Size - m_offset;
        const auto count = std::min(inputDataSize, freeSpace);
        copy(data.first(count));
        return count;
    }

    size_t push(T&& value)
    {
        return push(gsl::span<T, 1>(&value, 1));
    }

    /*!
     * @brief pop from fifo to data
     * 
     * @param data 
     * @return size_t really popped size from fifo
     */
    size_t pop(gsl::span<T> data)
    {
        const auto requestSize = data.size();
        const auto count = std::min(requestSize, m_offset);
        copy(data, gsl::span(m_fifo).first(count));

        if (requestSize < m_offset) {
            // move back
            std::memmove(m_fifo.data(), m_fifo.data() + count, m_offset * sizeof(T));
        }

        m_offset -= count;
        return count;
    }

    [[nodiscard]] std::optional<T> pop()
    {
        T val{};
        if (pop(gsl::span<T, 1>(&val, 1)) == 0) {
            return std::nullopt;
        }
        return val;
    }

private:
    static void copy(gsl::span<T> dst, gsl::span<const T> src)
    {
        std::memcpy(dst.data(), src.data(), src.size() * sizeof(T));
    }

    void copy(gsl::span<const T> data)
    {
        const auto count = data.size();
        copy(gsl::span(m_fifo).subspan(m_offset, count), data);
        m_offset += count;
    }
    std::array<T, Size> m_fifo{};
    size_t m_offset{};
};
}   // namespace nhope
