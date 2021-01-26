#include <stdexcept>
#include <thread>
#include <gtest/gtest.h>

#include <nhope/async/ao-context.h>
#include <nhope/async/delayed-property.h>

using namespace nhope;
using namespace std::literals;

TEST(DelayedProperty, simpleSet)   // NOLINT
{
    constexpr int testValue = 10;

    DelayedProperty prop(0);
    auto t = std::thread([&] {
        prop.applyNewValue([&](const int& v) mutable {
            EXPECT_FALSE(prop.hasNewValue());
            EXPECT_EQ(v, testValue);
        });
    });

    auto f = prop.setNewValue(testValue + 0);
    t.join();
    EXPECT_FALSE(prop.hasNewValue());

    EXPECT_EQ(f.get(), testValue);
}

TEST(DelayedProperty, exception)   // NOLINT
{
    constexpr int testValue = 10;
    DelayedProperty prop(0);
    auto f = prop.setNewValue(1);
    EXPECT_TRUE(prop.hasNewValue());
    auto f2 = prop.setNewValue(1);
    EXPECT_THROW(f.get(), std::runtime_error);   //NOLINT
    prop.applyNewValue();
    EXPECT_EQ(prop.getCurValue(), 1);
    EXPECT_EQ(f2.get(), 1);
}
