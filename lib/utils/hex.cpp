#include <cstdint>
#include <stdexcept>

#include "nhope/utils/hex.h"

namespace nhope {

HexParseError::HexParseError()
  : std::runtime_error("hex parse error")
{}

uint8_t hexToInt(const char v)
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
    throw HexParseError();
}

std::string removeWhitespaces(std::string_view s)
{
    std::string result;
    for (char i : s) {
        if (isspace(static_cast<unsigned char>(i)) == 0) {
            result += i;
        }
    }
    return result;
}

std::vector<uint8_t> fromHex(std::string_view hex)
{
    std::vector<uint8_t> res;
    const auto stripped = removeWhitespaces(hex);
    const auto strippedSize = stripped.size();
    if ((strippedSize & 0x1) != 0U) {
        throw HexParseError();
    }
    for (size_t i = 0; i < strippedSize; i += 2) {
        uint8_t value = hexToInt(stripped[i]) << 4 | hexToInt(stripped[i + 1]);
        res.emplace_back(value);
    }

    return res;
}

std::string toHex(gsl::span<const uint8_t> hex)
{
    std::string hexStr;
    hexStr.reserve(hex.size() * 2);
    for (auto v : hex) {
        const auto t = intToHex(v);
        hexStr.push_back(t[0]);
        hexStr.push_back(t[1]);
    }
    return hexStr;
}

}   // namespace nhope
