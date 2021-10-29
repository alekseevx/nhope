#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string_view>
#include <string>
#include <vector>

namespace nhope {

class HexParseError final : public std::runtime_error
{
public:
    explicit HexParseError(std::string_view msg);
};

std::uint8_t fromHex(char hi, char lo);
std::vector<uint8_t> fromHex(std::string_view hex);
std::string toHex(std::span<const uint8_t> bytes);

}   // namespace nhope
