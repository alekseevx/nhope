#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "gsl/span"

namespace nhope {

class Base64ParseError final : public std::runtime_error
{
public:
    explicit Base64ParseError(std::string_view msg);
};

std::vector<std::uint8_t> fromBase64(std::string_view str, bool skipSpaces = true);
std::string toBase64(gsl::span<const std::uint8_t> plainSeq);

}   // namespace nhope
