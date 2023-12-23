#pragma once

#include <array>
#include <cstddef>
#include <cstring>
#include <optional>
#include <type_traits>

#include <gsl/span>
#include <gsl/assert>

namespace nhope {

template<typename T, std::size_t Size>
class Fifo final
{
    static_assert(std::is_standard_layout_v<T> && std::is_trivial_v<T>);

    template<std::size_t Value>
    static constexpr std::size_t nextPowerOf2()
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

    void clear() noexcept
    {
        m_count = m_head = m_tail = 0;
    }

    std::size_t push(gsl::span<const T> data) noexcept
    {
        const auto freeSpace = Size - m_count;
        const auto count = std::min(data.size(), freeSpace);
        if (GSL_UNLIKELY(count == 0)) {
            return 0;
        }

        if (GSL_LIKELY(m_head + count <= capacity)) {
            copy(gsl::span(m_buffer).subspan(m_head, count), data.first(count));
            m_head = (m_head + count) & (capacity - 1);
        } else {
            const auto firstPartCount = capacity - m_head;
            auto src = gsl::span(m_buffer);
            copy(src.subspan(m_head), data.first(firstPartCount));
            m_head = (m_head + count) & (capacity - 1);
            copy(src.first(m_head), data.subspan(firstPartCount));
        }
        m_count += count;
        return count;
    }

    std::size_t push(const T& value) noexcept
    {
        return push(gsl::span<const T, 1>(&value, 1));
    }

    /*!
     * @brief pop from fifo to data
     * 
     * @param data 
     * @return size_t really popped size from fifo
     */
    std::size_t pop(gsl::span<T> data) noexcept
    {
        const auto count = std::min(data.size(), m_count);
        if (GSL_UNLIKELY(count == 0)) {
            return count;
        }

        if (GSL_LIKELY(m_tail + count <= capacity)) {
            copy(data, gsl::span(m_buffer).subspan(m_tail, count));
        } else {
            const auto firstPart = gsl::span(m_buffer).subspan(m_tail);
            const auto firstPartSize = firstPart.size();
            copy(data, firstPart);
            copy(gsl::span(data).subspan(firstPartSize), gsl::span(m_buffer).first(count - firstPartSize));
        }
        m_count -= count;
        m_tail = (m_tail + count) & (capacity - 1);

        return count;
    }

    [[nodiscard]] std::optional<T> pop() noexcept
    {
        T val{};
        if (pop(gsl::span<T, 1>(&val, 1)) == 0) {
            return std::nullopt;
        }
        return val;
    }

private:
    static void copy(gsl::span<T> dst, gsl::span<const T> src) noexcept
    {
        std::memcpy(dst.data(), src.data(), src.size() * sizeof(T));
    }

    static constexpr std::size_t capacity = nextPowerOf2<Size>();

    std::array<T, capacity> m_buffer{};
    std::size_t m_head{};
    std::size_t m_tail{};
    std::size_t m_count{};
};
}   // namespace nhope
