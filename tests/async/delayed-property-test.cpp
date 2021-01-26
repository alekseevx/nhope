#include <stdexcept>
#include <thread>
#include <gtest/gtest.h>

#include <nhope/async/ao-context.h>
#include <nhope/async/delayed-property.h>

namespace {

constexpr int testValue = 10;

using namespace nhope;
using namespace std::literals;

}   // namespace

TEST(DelayedProperty, simpleSet)   // NOLINT
{
    DelayedProperty prop(0);

    auto f = prop.setNewValue(testValue);
    auto t = std::thread([&] {
        prop.applyNewValue([&](const int& v) mutable {
            EXPECT_FALSE(prop.hasNewValue());
            EXPECT_EQ(v, testValue);
        });
    });
    t.join();
    f.get();
    EXPECT_FALSE(prop.hasNewValue());
}

TEST(DelayedProperty, exception)   // NOLINT
{
    DelayedProperty prop(0);
    auto f = prop.setNewValue(1);
    EXPECT_TRUE(prop.hasNewValue());
    auto f2 = prop.setNewValue(1);
    EXPECT_THROW(f.get(), std::runtime_error);   //NOLINT
    prop.applyNewValue();
    f2.get();
    EXPECT_EQ(prop.getCurrentValue(), 1);
}

TEST(DelayedProperty, waitPropertyTimeout)   // NOLINT
{
    DelayedProperty prop(0);
    auto t = std::thread([&] {
        EXPECT_TRUE(prop.waitNewValue(20ms));
        prop.applyNewValue();
        EXPECT_EQ(prop.getCurrentValue(), testValue);
    });
    std::this_thread::sleep_for(10ms);
    prop.setNewValue(testValue);

    t.join();
    EXPECT_EQ(prop.getCurrentValue(), testValue);

    std::thread([&] {
        std::this_thread::sleep_for(5ms);
        prop.setNewValue(1);
    }).detach();

    prop.waitNewValue();
    prop.applyNewValue();

    EXPECT_EQ(prop.getCurrentValue(), 1);
}
