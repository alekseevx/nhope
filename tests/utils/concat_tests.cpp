#include <gtest/gtest.h>
#include <string>

#include "nhope/utils/array.h"

using namespace nhope;
using namespace std::literals;

TEST(Concat, concatArray)   // NOLINT
{
    static constexpr auto errMsg = concatArrays(toArray("Something "), toArray<4>("test"sv), std::array<char, 1>{{0}});
    constexpr std::string_view str = errMsg.data();
    EXPECT_EQ(str, "Something test"sv);
}
