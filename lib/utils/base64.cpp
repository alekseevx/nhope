#include <cstddef>
#include <cstdint>
#include <string>

#include "fmt/format.h"
#include "gsl/span"

#include "nhope/utils/base64.h"
#include "nhope/utils/string-utils.h"

namespace nhope {

namespace {
using namespace std::literals;

std::uint8_t decode(char c)
{
    if (c >= 'A' && c <= 'Z') {
        return static_cast<std::uint8_t>(c - 'A');
    }

    if (c >= 'a' && c <= 'z') {
        return static_cast<std::uint8_t>(c - 'a' + 26);
    }

    if (c >= '0' && c <= '9') {
        return static_cast<std::uint8_t>(c - '0' + 52);
    }

    if (c == '+') {
        return 62;
    }

    if (c == '/') {
        return 63;
    }

    throw Base64ParseError(fmt::format("'{}': invalid symbol", c));
}

std::vector<std::uint8_t> fromBase64Impl(std::string_view str)
{
    const auto strLen = str.length();

    if (strLen == 0) {
        return {};
    }

    if (strLen % 4 != 0) {
        throw Base64ParseError("Illegal input length");
    }

    const auto totalBytes = strLen * 3 / 4 + 1;
    std::vector<std::uint8_t> retval;
    retval.reserve(totalBytes);

    const auto n = str.back() == '=' ? strLen - 4 : strLen;
    for (std::size_t i = 0; i < n; i += 4) {
        const auto by1 = decode(str[i]);
        const auto by2 = decode(str[i + 1]);
        const auto by3 = decode(str[i + 2]);
        const auto by4 = decode(str[i + 3]);

        retval.push_back(static_cast<std::uint8_t>(by1 << 2) | (by2 >> 4));
        retval.push_back(static_cast<std::uint8_t>((by2 & 0xf) << 4) | (by3 >> 2));
        retval.push_back(static_cast<std::uint8_t>((by3 & 0x3) << 6) | by4);
    }

    if (str.substr(strLen - 2) == "=="sv) {
        const auto by1 = decode(str[strLen - 4]);
        const auto by2 = decode(str[strLen - 3]);
        retval.push_back(static_cast<std::uint8_t>(by1 << 2) | (by2 >> 4));
    } else if (str.back() == '=') {
        const auto by1 = decode(str[strLen - 4]);
        const auto by2 = decode(str[strLen - 3]);
        const auto by3 = decode(str[strLen - 2]);
        retval.push_back(static_cast<std::uint8_t>(by1 << 2) | (by2 >> 4));
        retval.push_back(static_cast<std::uint8_t>((by2 & 0xf) << 4) | (by3 >> 2));
    }

    return retval;
}

}   // namespace

Base64ParseError::Base64ParseError(std::string_view msg)
  : std::runtime_error(fmt::format("Base64 parse error: {}", msg))
{}

std::vector<std::uint8_t> fromBase64(std::string_view str, bool skipSpaces)
{
    if (skipSpaces) {
        return fromBase64Impl(removeWhitespaces(str));
    }

    return fromBase64Impl(str);
}

std::string toBase64(gsl::span<const std::uint8_t> plainSeq)
{
    constexpr auto table = "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                           "abcdefghijklmnopqrstuvwxyz"
                           "0123456789+/"sv;

    const auto plainSize = plainSeq.size();

    // Reserve enough space for the returned base64 string
    const auto base64Bytes = (((plainSeq.size() * 4) / 3) + 1);
    const auto newlineBytes = (((base64Bytes * 2) / 76) + 1);
    const auto totalBytes = base64Bytes + newlineBytes;

    std::string retval;
    retval.reserve(totalBytes);

    std::size_t i = 0;
    for (i = 0; i + 2 < plainSize; i += 3) {
        const auto by1 = plainSeq[i];
        const auto by2 = plainSeq[i + 1];
        const auto by3 = plainSeq[i + 2];

        retval += table[by1 >> 2];
        retval += table[((by1 & 0x3) << 4) | (by2 >> 4)];
        retval += table[((by2 & 0xf) << 2) | (by3 >> 6)];
        retval += table[by3 & 0x3f];
    }

    if (i + 1 == plainSize) {
        const auto by1 = plainSeq[i];

        retval += table[by1 >> 2];
        retval += table[(by1 & 0x3) << 4];
        retval += "=="sv;
    } else if (i + 2 == plainSize) {
        const auto by1 = plainSeq[i];
        const auto by2 = plainSeq[i + 1];

        retval += table[by1 >> 2];
        retval += table[((by1 & 0x3) << 4) | (by2 >> 4)];
        retval += table[(by2 & 0xf) << 2];
        retval += "="sv;
    }

    return retval;
}

}   // namespace nhope
