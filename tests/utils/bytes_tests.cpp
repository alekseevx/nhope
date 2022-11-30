#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include <nhope/utils/bytes.h>

namespace {
using namespace nhope;
using namespace std::literals;

using VBytes = std::vector<std::uint8_t>;

template<size_t N>
bool eq(const std::array<std::uint8_t, N>& a, const VBytes& b)
{
    for (size_t i = 0; i < N; ++i) {
        if (a.at(i) != b.at(i)) {
            return false;
        }
    }

    return true;
}

}   // namespace

TEST(Bytes, toBytes)   // NOLINT
{
    EXPECT_TRUE(eq(toBytes<std::uint16_t>(0x0201, Endian::Little), VBytes({1, 2})));
    EXPECT_TRUE(eq(toBytes<std::uint16_t>(0x0201, Endian::Big), VBytes({2, 1})));

    EXPECT_TRUE(eq(toBytes<std::uint32_t>(0x04030201, Endian::Little), VBytes({1, 2, 3, 4})));
    EXPECT_TRUE(eq(toBytes<std::uint32_t>(0x04030201, Endian::Big), VBytes({4, 3, 2, 1})));

    EXPECT_TRUE(eq(toBytes<std::uint64_t>(0x0807060504030201, Endian::Little), VBytes({1, 2, 3, 4, 5, 6, 7, 8})));
    EXPECT_TRUE(eq(toBytes<std::uint64_t>(0x0807060504030201, Endian::Big), VBytes({8, 7, 6, 5, 4, 3, 2, 1})));
}

TEST(Bytes, fromBytes)   // NOLINT
{
    EXPECT_EQ(fromBytes<std::uint16_t>(VBytes({1, 2}), Endian::Little), 0x0201);
    EXPECT_EQ(fromBytes<std::uint16_t>(VBytes({2, 1}), Endian::Big), 0x0201);

    EXPECT_EQ(fromBytes<std::uint32_t>(VBytes({1, 2, 3, 4}), Endian::Little), 0x04030201);
    EXPECT_EQ(fromBytes<std::uint32_t>(VBytes({4, 3, 2, 1}), Endian::Big), 0x04030201);

    EXPECT_EQ(fromBytes<std::uint64_t>(VBytes({1, 2, 3, 4, 5, 6, 7, 8}), Endian::Little), 0x0807060504030201);
    EXPECT_EQ(fromBytes<std::uint64_t>(VBytes({8, 7, 6, 5, 4, 3, 2, 1}), Endian::Big), 0x0807060504030201);
}

TEST(Bytes, bytesSwap)   // NOLINT
{
    EXPECT_EQ(bytesSwap(std::uint16_t(0xABCD)), 0xCDAB);
    EXPECT_EQ(bytesSwap(std::uint32_t(0xABCDEFDD)), 0xDDEFCDAB);
    EXPECT_EQ(bytesSwap(std::uint64_t(0x0102030405060708)), 0x0807060504030201);
}
