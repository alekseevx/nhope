#include <cstdint>
#include <stdexcept>

#include "fmt/format.h"

#include "nhope/utils/hex.h"
#include "nhope/utils/string-utils.h"

namespace nhope {

HexParseError::HexParseError(const std::string& msg)
  : std::runtime_error(msg)
{}

namespace {

constexpr std::array<char, 2> byteToHex(uint8_t v)
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

uint8_t hexToByte(const char v)
{
    constexpr auto ten{10};

    if (v >= '0' && v <= '9') {
        return v - '0';
    }
    if (v >= 'a' && v <= 'f') {
        return ten + (v - 'a');
    }
    if (v >= 'A' && v <= 'F') {
        return ten + (v - 'A');
    }
    throw HexParseError(fmt::format("hex parse error: invalid value {}", int(v)));
}

}   // namespace

std::vector<uint8_t> fromHex(std::string_view hex)
{
    std::vector<uint8_t> res;
    res.reserve(hex.size() / 2);
    const auto stripped = removeWhitespaces(hex);
    const auto strippedSize = stripped.size();
    if ((strippedSize & 0x1) != 0U) {
        throw HexParseError(fmt::format("incorrect size {}: must be even", hex.size()));
    }
    for (size_t i = 0; i < strippedSize; i += 2) {
        uint8_t value = hexToByte(stripped[i]) << 4 | hexToByte(stripped[i + 1]);
        res.emplace_back(value);
    }

    return res;
}

std::string toHex(gsl::span<const uint8_t> bytes)
{
    std::string hexStr;
    hexStr.reserve(bytes.size() * 2);
    for (auto v : bytes) {
        const auto t = byteToHex(v);
        hexStr.push_back(t[0]);
        hexStr.push_back(t[1]);
    }
    return hexStr;
}

}   // namespace nhope
