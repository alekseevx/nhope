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
    explicit HexParseError(const std::string& msg);
};

std::vector<uint8_t> fromHex(std::string_view hex);
std::string toHex(gsl::span<const uint8_t> bytes);

std::string removeWhitespaces(std::string_view s);

}   // namespace nhope
