#pragma once

#include "gsl/span"
#include <array>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace nhope {

class HexParseError final : public std::runtime_error
{
public:
    HexParseError();
};

uint8_t hexToInt(char v);

constexpr inline std::array<char, 2> intToHex(uint8_t v)
{
    auto hx = [](uint8_t p) -> char {
        constexpr auto ten{10};
        if (p >= ten) {
            return static_cast<char>('a' + (p - ten));
        }
        return static_cast<char>('0' + p);
    };
    //NOLINTNEXTLINE (readability-magic-numbers)
    return {hx(v >> 4), hx(v & 0xF)};
}

std::vector<uint8_t> fromHex(std::string_view hex);
std::string toHex(gsl::span<const uint8_t> hex);

constexpr inline bool isAlpha(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

constexpr inline bool isDigit(char c)
{
    return c >= '0' && c <= '9';
}

std::string removeWhitespaces(std::string_view s);

}   // namespace nhope
