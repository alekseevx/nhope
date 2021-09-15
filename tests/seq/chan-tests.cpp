#include <climits>

#include <gtest/gtest.h>

#include <nhope/seq/chan.h>
#include <nhope/seq/func-producer.h>
#include "nhope/seq/consumer-list.h"

namespace {
using namespace nhope;

int sum(Chan<int>& chan, int n = INT_MAX)
{
    int result = 0;
    int value = 0;
    while (n > 0 && chan.get(value)) {
        result += value;
        --n;
    }

    return result;
}

int count(Chan<int>& chan, int n = INT_MAX)
{
    int result = 0;
    int value = 0;
    while (n > 0 && chan.get(value)) {
        ++result;
        --n;
    }

    return result;
}
}   // namespace

TEST(ChanTest, OneToOne)   //NOLINT
{
    static constexpr int MaxProduseCount = 1'000'000;
    static constexpr int ChanCapacity = 100;
    static constexpr int SumLimit = 1000;

    FuncProducer<int> numProducer([m = 0](int& value) mutable -> bool {
        if (m >= MaxProduseCount) {
            return false;
        }

        value = m++;
        return true;
    });

    Chan<int> chan(true, ChanCapacity);
    chan.attachToProducer(numProducer);
    numProducer.start();

    int res = sum(chan, SumLimit);
    GTEST_ASSERT_EQ(res, 499'500);
}

TEST(ChanTest, ManyToOne)   // NOLINT
{
    static constexpr int MaxProduseCount = 1'000'000;
    static constexpr int ChanCapacity = 10;
    static constexpr int CountLimit = 100'000;

    FuncProducer<int> evenNumProducer([m = 0](int& value) mutable -> bool {
        if (m >= MaxProduseCount) {
            return false;
        }

        value = 2 * m++;
        return true;
    });

    FuncProducer<int> oddNumproducer([m = 0](int& value) mutable -> bool {
        if (m >= MaxProduseCount) {
            return false;
        }

        value = (2 * m++) + 1;
        return true;
    });

    Chan<int> chan(true, ChanCapacity);
    chan.attachToProducer(evenNumProducer);
    chan.attachToProducer(oddNumproducer);
    evenNumProducer.start();
    oddNumproducer.start();

    int res = count(chan, CountLimit);
    GTEST_ASSERT_EQ(res, 100'000);
}

TEST(ChanTest, OneToMany)   // NOLINT
{
    static constexpr int MaxProduseCount = 1'000;
    static constexpr int ChanCapacity = 100;
    static constexpr int Chan2Capacity = 100;

    FuncProducer<int> numProducer([m = 0](int& value) mutable -> bool {
        if (m >= MaxProduseCount) {
            return false;
        }

        value = m++;
        return true;
    });

    Chan<int> chan(true, ChanCapacity);
    numProducer.attachConsumer(chan.makeInput());

    auto thread = std::thread([&chan]() {
        int res = sum(chan);
        GTEST_ASSERT_EQ(res, 499'500);
    });

    Chan<int> chan2(true, Chan2Capacity);
    numProducer.attachConsumer(chan2.makeInput());

    auto thread2 = std::thread([&chan2]() {
        int res = count(chan2);
        GTEST_ASSERT_EQ(res, 1000);
    });

    numProducer.start();

    thread.join();
    thread2.join();
}

TEST(ConsumerListTest, Closed)   //NOLINT
{
    Chan<int> chan(true);
    ConsumerList<int> cl;
    cl.close();
    cl.addConsumer(chan.makeInput());
    int x{};
    cl.consume(x);
}
