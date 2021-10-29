#include <array>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string_view>

#include "fmt/format.h"

#include "nhope/utils/hex.h"
#include "nhope/utils/string-utils.h"

namespace nhope {
using namespace std::literals;

HexParseError::HexParseError(const std::string_view msg)
  : std::runtime_error(fmt::format("hex parse error: {}", msg))
{}

namespace {

constexpr std::array<char, 2> byteToHex(uint8_t v)
{
    constexpr auto hex = "0123456789abcdef"sv;
    //NOLINTNEXTLINE (readability-magic-numbers)
    return {hex[v >> 4], hex[v & 0xF]};
}

constexpr auto invalidHexValue = 16;

constexpr auto makeDecodeTable()
{
    constexpr auto ten = 10;
    std::array<std::uint8_t, UCHAR_MAX + 1> table{};
    for (std::size_t ch = 0; ch < table.size(); ++ch) {
        if (ch >= '0' && ch <= '9') {
            table[ch] = static_cast<std::uint8_t>(ch - '0');
        } else if (ch >= 'a' && ch <= 'f') {
            table[ch] = static_cast<std::uint8_t>(ch - 'a' + ten);
        } else if (ch >= 'A' && ch <= 'F') {
            table[ch] = static_cast<std::uint8_t>(ch - 'A' + ten);
        } else {
            table[ch] = invalidHexValue;
        }
    }

    return table;
}

constexpr auto decodeTable = makeDecodeTable();

std::uint8_t fromHex(char ch)
{
    const std::uint8_t val = decodeTable[static_cast<std::uint8_t>(ch)];
    if (val == invalidHexValue) {
        throw HexParseError(fmt::format("invalid value {}", ch));
    }

    return val;
}

}   // namespace

std::uint8_t fromHex(char hi, char lo)
{
    return (fromHex(hi) << 4) | fromHex(lo);
}

std::vector<uint8_t> fromHex(std::string_view hex)
{
    std::vector<uint8_t> res;
    res.reserve(hex.size() / 2);
    const auto stripped = removeWhitespaces(hex);
    const auto strippedSize = stripped.size();
    if ((strippedSize & 0x1) != 0U) {
        throw HexParseError(fmt::format("incorrect size {}: must be even", hex.size()));
    }
    for (std::size_t i = 0; i < strippedSize; i += 2) {
        const auto byte = fromHex(stripped[i], stripped[i + 1]);
        res.push_back(byte);
    }

    return res;
}

std::string toHex(std::span<const uint8_t> bytes)
{
    std::string hexStr;
    hexStr.reserve(bytes.size() * 2);
    for (auto v : bytes) {
        const auto t = byteToHex(v);
        hexStr.append(t.data(), t.size());
    }
    return hexStr;
}

}   // namespace nhope
