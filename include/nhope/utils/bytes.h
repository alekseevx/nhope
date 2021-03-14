#pragma once

#include <array>
#include <cstdint>

#include <gsl/span>

#include <nhope/utils/bits.h>

namespace nhope {

inline constexpr void toBytes(std::uint16_t val, gsl::span<std::uint8_t, 2> bytes, Endian byteOrder)
{
    switch (byteOrder) {
    case Endian::Little:
        bytes[0] = static_cast<std::uint8_t>(val);
        bytes[1] = static_cast<std::uint8_t>(val >> 8);   // NOLINT(readability-magic-numbers)
        break;

    case Endian::Big:
        bytes[0] = static_cast<std::uint8_t>(val >> 8);   // NOLINT(readability-magic-numbers)
        bytes[1] = static_cast<std::uint8_t>(val);
        break;
    }
}

inline constexpr void toBytes(std::uint32_t val, gsl::span<std::uint8_t, 4> bytes, Endian byteOrder)
{
    switch (byteOrder) {
    case Endian::Little:
        bytes[0] = static_cast<std::uint8_t>(val);
        bytes[1] = static_cast<std::uint8_t>(val >> 8);    // NOLINT(readability-magic-numbers)
        bytes[2] = static_cast<std::uint8_t>(val >> 16);   // NOLINT(readability-magic-numbers)
        bytes[3] = static_cast<std::uint8_t>(val >> 24);   // NOLINT(readability-magic-numbers)
        break;

    case Endian::Big:
        bytes[0] = static_cast<std::uint8_t>(val >> 24);   // NOLINT(readability-magic-numbers)
        bytes[1] = static_cast<std::uint8_t>(val >> 16);   // NOLINT(readability-magic-numbers)
        bytes[2] = static_cast<std::uint8_t>(val >> 8);    // NOLINT(readability-magic-numbers)
        bytes[3] = static_cast<std::uint8_t>(val);
        break;
    }
}

// NOLINTNEXTLINE(readability-magic-numbers)
inline constexpr void toBytes(std::uint64_t val, gsl::span<std::uint8_t, 8> bytes, Endian byteOrder)
{
    switch (byteOrder) {
    case Endian::Little:
        bytes[0] = static_cast<std::uint8_t>(val);
        bytes[1] = static_cast<std::uint8_t>(val >> 8);    // NOLINT(readability-magic-numbers)
        bytes[2] = static_cast<std::uint8_t>(val >> 16);   // NOLINT(readability-magic-numbers)
        bytes[3] = static_cast<std::uint8_t>(val >> 24);   // NOLINT(readability-magic-numbers)
        bytes[4] = static_cast<std::uint8_t>(val >> 32);   // NOLINT(readability-magic-numbers)
        bytes[5] = static_cast<std::uint8_t>(val >> 40);   // NOLINT(readability-magic-numbers)
        bytes[6] = static_cast<std::uint8_t>(val >> 48);   // NOLINT(readability-magic-numbers)
        bytes[7] = static_cast<std::uint8_t>(val >> 56);   // NOLINT(readability-magic-numbers)
        break;

    case Endian::Big:
        bytes[0] = static_cast<std::uint8_t>(val >> 56);   // NOLINT(readability-magic-numbers)
        bytes[1] = static_cast<std::uint8_t>(val >> 48);   // NOLINT(readability-magic-numbers)
        bytes[2] = static_cast<std::uint8_t>(val >> 40);   // NOLINT(readability-magic-numbers)
        bytes[3] = static_cast<std::uint8_t>(val >> 32);   // NOLINT(readability-magic-numbers)
        bytes[4] = static_cast<std::uint8_t>(val >> 24);   // NOLINT(readability-magic-numbers)
        bytes[5] = static_cast<std::uint8_t>(val >> 16);   // NOLINT(readability-magic-numbers)
        bytes[6] = static_cast<std::uint8_t>(val >> 8);    // NOLINT(readability-magic-numbers)
        bytes[7] = static_cast<std::uint8_t>(val);         // NOLINT(readability-magic-numbers)
        break;
    }
}

template<typename Int>
constexpr auto toBytes(Int val, Endian byteOrder)
{
    std::array<std::uint8_t, sizeof(Int)> bytes{};
    toBytes(val, bytes, byteOrder);
    return bytes;
}

inline constexpr void fromBytes(std::uint16_t& var, gsl::span<const std::uint8_t, 2> bytes, Endian byteOrder)
{
    switch (byteOrder) {
    case Endian::Little:
        var = static_cast<std::uint16_t>(bytes[0])             //
              | (static_cast<std::uint16_t>(bytes[1]) << 8);   // NOLINT(readability-magic-numbers)
        break;

    case Endian::Big:
        var = (static_cast<std::uint16_t>(bytes[0]) << 8)   // NOLINT(readability-magic-numbers)
              | static_cast<std::uint16_t>(bytes[1]);       //
        break;
    }
}

inline constexpr void fromBytes(std::uint32_t& var, gsl::span<const std::uint8_t, 4> bytes, Endian byteOrder)
{
    switch (byteOrder) {
    case Endian::Little:
        var = (static_cast<std::uint32_t>(bytes[0]) << 0)       //
              | (static_cast<std::uint32_t>(bytes[1]) << 8)     // NOLINT(readability-magic-numbers)
              | (static_cast<std::uint32_t>(bytes[2]) << 16)    // NOLINT(readability-magic-numbers)
              | (static_cast<std::uint32_t>(bytes[3]) << 24);   // NOLINT(readability-magic-numbers)
        break;

    case Endian::Big:
        var = (static_cast<std::uint32_t>(bytes[0]) << 24)     // NOLINT(readability-magic-numbers)
              | (static_cast<std::uint32_t>(bytes[1]) << 16)   // NOLINT(readability-magic-numbers)
              | (static_cast<std::uint32_t>(bytes[2]) << 8)    // NOLINT(readability-magic-numbers)
              | (static_cast<std::uint32_t>(bytes[3]) << 0);   //
        break;
    }
}

// NOLINTNEXTLINE(readability-magic-numbers)
inline constexpr void fromBytes(std::uint64_t& var, gsl::span<const std::uint8_t, 8> bytes, Endian byteOrder)
{
    switch (byteOrder) {
    case Endian::Little:
        var = (static_cast<std::uint64_t>(bytes[0]) << 0)       //
              | (static_cast<std::uint64_t>(bytes[1]) << 8)     // NOLINT(readability-magic-numbers)
              | (static_cast<std::uint64_t>(bytes[2]) << 16)    // NOLINT(readability-magic-numbers)
              | (static_cast<std::uint64_t>(bytes[3]) << 24)    // NOLINT(readability-magic-numbers)
              | (static_cast<std::uint64_t>(bytes[4]) << 32)    // NOLINT(readability-magic-numbers)
              | (static_cast<std::uint64_t>(bytes[5]) << 40)    // NOLINT(readability-magic-numbers)
              | (static_cast<std::uint64_t>(bytes[6]) << 48)    // NOLINT(readability-magic-numbers)
              | (static_cast<std::uint64_t>(bytes[7]) << 56);   // NOLINT(readability-magic-numbers)
        break;

    case Endian::Big:
        var = (static_cast<std::uint64_t>(bytes[0]) << 56)     // NOLINT(readability-magic-numbers)
              | (static_cast<std::uint64_t>(bytes[1]) << 48)   // NOLINT(readability-magic-numbers)
              | (static_cast<std::uint64_t>(bytes[2]) << 40)   // NOLINT(readability-magic-numbers)
              | (static_cast<std::uint64_t>(bytes[3]) << 32)   // NOLINT(readability-magic-numbers)
              | (static_cast<std::uint64_t>(bytes[4]) << 24)   // NOLINT(readability-magic-numbers)
              | (static_cast<std::uint64_t>(bytes[5]) << 16)   // NOLINT(readability-magic-numbers)
              | (static_cast<std::uint64_t>(bytes[6]) << 8)    // NOLINT(readability-magic-numbers)
              | (static_cast<std::uint64_t>(bytes[7]) << 0);   // NOLINT(readability-magic-numbers)
        break;
    }
}

template<typename Int>
constexpr Int fromBytes(gsl::span<const std::uint8_t> bytes, Endian byteOrder)
{
    Int retval{};
    fromBytes(retval, bytes.first<sizeof(Int)>(), byteOrder);
    return retval;
}

}   // namespace nhope
