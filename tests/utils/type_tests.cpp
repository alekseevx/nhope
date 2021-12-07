#include <gtest/gtest.h>

#include "nhope/utils/type.h"

using namespace nhope;

TEST(Types, Functions)   // NOLINT
{
    auto f = [](double&, float, int p) {
        return p + 2;
    };

    using func = decltype(f);

    constexpr bool okCheck = nhope::checkFunctionSignatureV<func, int, double&, float, int>;
    EXPECT_TRUE(okCheck);

    constexpr bool notOk = nhope::checkFunctionSignatureV<func, int, double&, float>;
    EXPECT_FALSE(notOk);

    constexpr bool notExactly = nhope::checkFunctionSignatureV<func, int, double, float, int>;
    EXPECT_FALSE(notExactly);
}
