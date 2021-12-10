#include <gtest/gtest.h>
#include <string>
#include <map>
#include <type_traits>

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

    constexpr bool notExtrimely = nhope::checkFunctionSignatureV<int, double, float, int>;
    EXPECT_FALSE(notExtrimely);

    constexpr bool notRet = nhope::checkReturnTypeV<int, double>;
    EXPECT_FALSE(notRet);

    constexpr bool notParam = nhope::checkFunctionParamsV<double, double>;
    EXPECT_FALSE(notParam);
}

TEST(Types, Functional)   // NOLINT
{
    EXPECT_FALSE(nhope::isFunctional<int>());
    EXPECT_FALSE(nhope::isFunctional<std::string>());
    EXPECT_FALSE(nhope::isFunctional<char*>());
    EXPECT_FALSE(nhope::isFunctional<void>());

    struct Test
    {
        [[nodiscard]] int get() const
        {
            return x;
        }
        int y{1};

    private:
        int x{2};
    };

    EXPECT_FALSE(nhope::isFunctional<Test>());
    using SomeMap = std::map<int, double>;

    EXPECT_FALSE(nhope::isFunctional<SomeMap>());
    EXPECT_TRUE(nhope::isFunctional<decltype(&Test::get)>());
    EXPECT_FALSE(nhope::isFunctional<decltype(&Test::y)>());

    EXPECT_TRUE(nhope::isFunctional<int()>());
    EXPECT_TRUE(nhope::isFunctional<std::function<int()>>());
    int val{2};
    auto lambda = [=](int) {
        return 1 + val;
    };
    EXPECT_TRUE(nhope::isFunctional<decltype(lambda)>());
}
