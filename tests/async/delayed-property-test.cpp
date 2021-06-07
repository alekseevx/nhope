#include <atomic>
#include <stdexcept>
#include <thread>
#include <gtest/gtest.h>

#include <nhope/async/ao-context.h>
#include <nhope/async/delayed-property.h>
#include <nhope/seq/func-produser.h>

namespace {

constexpr int testValue = 10;
constexpr auto maxProduseCount{100};

constexpr auto nullHandler = [](auto /*unused*/) {};

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

TEST(DelayedProperty, construct)   // NOLINT
{
    static constexpr int vectorSize{100};
    
    DelayedProperty<int> prop;


    DelayedProperty<std::vector<int>> prop1(vectorSize, 3);
    ASSERT_EQ(prop1.getCurrentValue().size(), vectorSize);
    ASSERT_EQ(prop1.getCurrentValue().at(1), 3);
}

TEST(DelayedProperty, exception)   // NOLINT
{
    DelayedProperty prop(0);
    prop.applyNewValue([](auto /*unused*/) {
        FAIL() << "value not changed";
    });
    auto f = prop.setNewValue(1);
    EXPECT_TRUE(prop.hasNewValue());
    auto f2 = prop.setNewValue(1);
    EXPECT_THROW(f.get(), nhope::AsyncOperationWasCancelled);   //NOLINT
    prop.applyNewValue([](auto /*unused*/) {
        throw std::runtime_error("some error");
    });
    EXPECT_THROW(f2.get(), std::runtime_error);   //NOLINT
    EXPECT_EQ(prop.getCurrentValue(), 0);
}

TEST(DelayedProperty, waitPropertyTimeout)   // NOLINT
{
    DelayedProperty prop(0);
    auto t = std::thread([&] {
        EXPECT_TRUE(prop.waitNewValue(100ms));
        prop.applyNewValue(nullHandler);
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
    prop.applyNewValue(nullHandler);

    EXPECT_EQ(prop.getCurrentValue(), 1);
}

TEST(DelayedProperty, setEqualValue)   // NOLINT
{
    DelayedProperty prop(testValue);

    auto f = prop.setNewValue(testValue);
    EXPECT_FALSE(prop.hasNewValue());
    f.get();
    EXPECT_EQ(prop.getCurrentValue(), testValue);
}

TEST(DelayedProperty, producer)   // NOLINT
{
    DelayedProperty prop(testValue);

    FuncProduser<int> numProduser([m = 0](int& value) mutable -> bool {
        if (m == maxProduseCount + 1) {
            return false;
        }

        value = m++;
        return true;
    });

    prop.attachToProduser(numProduser);

    numProduser.start();
    numProduser.wait();
    prop.applyNewValue(nullHandler);
    EXPECT_EQ(prop.getCurrentValue(), maxProduseCount);
}

TEST(DelayedProperty, destroyBeforeProducerEnd)   // NOLINT
{
    std::atomic_bool close{false};

    FuncProduser<int> numProduser([m = 0, &close](int& value) mutable -> bool {
        if (close.load()) {
            return false;
        }

        value = m++;
        return true;
    });
    numProduser.start();

    {
        DelayedProperty prop(testValue);
        prop.attachToProduser(numProduser);
        prop.attachToProduser(numProduser);
        prop.attachToProduser(numProduser);
        std::this_thread::sleep_for(2ms);

        int value = 0;
        prop.applyNewValue([&value](auto val) {
            value = val;
        });
        EXPECT_EQ(prop.getCurrentValue(), value);
    }
    std::this_thread::sleep_for(20ms);
    close.store(true);

    numProduser.wait();
}