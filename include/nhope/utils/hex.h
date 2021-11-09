#pragma once

#include <array>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <gsl/span>

namespace nhope {

class HexParseError final : public std::runtime_error
{
public:
    explicit HexParseError(std::string_view msg);
};

std::uint8_t fromHex(char hi, char lo);
std::vector<uint8_t> fromHex(std::string_view hex);
std::string toHex(gsl::span<const uint8_t> bytes);

}   // namespace nhope
