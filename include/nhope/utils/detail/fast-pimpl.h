#pragma once

#include <cstddef>
#include <type_traits>
#include <utility>

namespace nhope::detail {
inline constexpr size_t defaultAlign = 8;
template<class T, size_t Size, size_t Alignment = defaultAlign>
class FastPimpl final
{
public:
    template<typename... Args>
    explicit FastPimpl(Args&&... args)
    {
        new (&m_data) T(std::forward<Args>(args)...);
    }

    ~FastPimpl() noexcept
    {
        validate<sizeof(T), alignof(T)>();

        //NOLINTNEXTLINE (cppcoreguidelines-pro-type-reinterpret-cast)
        reinterpret_cast<T*>(&m_data)->~T();
    }

    FastPimpl& operator=(FastPimpl&& rhs) noexcept
    {
        *get() = std::move(*rhs);
        return *this;
    }

    const T* operator->() const noexcept
    {
        return get();
    }

    T* operator->() noexcept
    {
        return get();
    }

    const T& operator*() const noexcept
    {
        return *get();
    }

    T& operator*() noexcept
    {
        return *get();
    }

    friend inline void swap(FastPimpl& lhs, FastPimpl& rhs)
    {
        using std::swap;
        swap(*lhs, *rhs);
    }

private:
    T* get() noexcept
    {
        //NOLINTNEXTLINE (cppcoreguidelines-pro-type-reinterpret-cast)
        return reinterpret_cast<T*>(&m_data);
    }
    const T* get() const noexcept
    {
        //NOLINTNEXTLINE (cppcoreguidelines-pro-type-reinterpret-cast)
        return reinterpret_cast<T*>(&m_data);
    }

    template<size_t ActualSize, size_t ActualAlignment>
    static void validate() noexcept
    {
        static_assert(Size >= ActualSize, "not enough Size for (T)");
        static_assert(Alignment >= ActualAlignment, "not enough Alignment size; alignof(T) mismatch");
    }

    mutable std::aligned_storage_t<Size, Alignment> m_data;
};

}   // namespace nhope::detail