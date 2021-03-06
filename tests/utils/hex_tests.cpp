#include <array>
#include <cstdint>
#include <gtest/gtest.h>

#include "nhope/utils/hex.h"
#include "nhope/utils/string-utils.h"

using namespace std::literals;
using namespace nhope;

namespace {

const auto etalon = std::vector<uint8_t>{
  0xa6, 0xe7, 0xd3, 0xb4, 0x6f, 0xdf, 0xaf, 0x0b, 0xde, 0x2a, 0x1f, 0x83, 0x2a, 0x00, 0xd2, 0xde,
};

constexpr auto etalonStr = "a6 e7 d3 b4 6f df af 0b de 2a 1f 83 2a 00 d2 de"sv;

}   // namespace

TEST(Hex, fromHex)   // NOLINT
{
    auto hex = fromHex("A6 E7 d3 b4 6f df af 0b de 2a 1f 83 2a 00 d2 de"sv);
    EXPECT_EQ(hex, etalon);
    EXPECT_THROW(fromHex("A6.E7"sv), HexParseError);   //NOLINT
    EXPECT_THROW(fromHex("A6Z7"sv), HexParseError);    //NOLINT
}

TEST(Hex, toHex)   // NOLINT
{
    const auto hex = toHex(etalon);
    EXPECT_EQ(hex, removeWhitespaces(etalonStr));
}
