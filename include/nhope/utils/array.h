#pragma once

#include <array>
#include <cstddef>
#include <string_view>

namespace nhope {

template<std::size_t N>
constexpr std::array<char, N> toArray(std::string_view val)
{
    std::array<char, N> res{};
    for (size_t i = 0; i < N; ++i) {
        res[i] = val[i];
    }
    return res;
}

template<std::size_t N>
constexpr std::array<char, N - 1> toArray(const char (&a)[N])   // NOLINT cppcoreguidelines-avoid-c-arrays
{
    std::array<char, N - 1> res{};
    for (std::size_t i = 0; i != N - 1; ++i) {
        res[i] = a[i];
    }
    return res;
}

template<std::size_t... Ns>
constexpr std::array<char, (Ns + ...)> concatArrays(const std::array<char, Ns>&... as)
{
    std::array<char, (Ns + ...)> res{};
    auto l = [&, i = 0](const auto& a) mutable {
        for (auto c : a) {
            res[i++] = c;
        }
    };

    (l(as), ...);
    return res;
}

}   // namespace nhope
