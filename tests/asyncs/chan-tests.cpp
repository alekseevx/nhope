#include <limits.h>

#include <gtest/gtest.h>

#include <nhope/asyncs/chan.h>
#include <nhope/asyncs/func-produser.h>

namespace {
using namespace nhope::asyncs;

int sum(Chan<int>& chan, int n = INT_MAX)
{
    int result = 0;
    int value;
    while (n > 0 && chan.get(value)) {
        result += value;
        --n;
    }

    return result;
}

int count(Chan<int>& chan, int n = INT_MAX)
{
    int result = 0;
    int value;
    while (n > 0 && chan.get(value)) {
        ++result;
        --n;
    }

    return result;
}
}   // namespace

TEST(ChanTest, OneToOne)
{
    FuncProduser<int> numProduser([m = 0](int& value) mutable -> bool {
        if (m >= 1'000'000) {
            return false;
        }

        value = m++;
        return true;
    });

    Chan<int> chan(true, 100);
    chan.attachToProduser(numProduser);
    numProduser.start();

    int res = sum(chan, 1000);
    GTEST_ASSERT_EQ(res, 499'500);
}

TEST(ChanTest, ManyToOne)
{
    FuncProduser<int> evenNumProduser([m = 0](int& value) mutable -> bool {
        if (m >= 1'000'000) {
            return false;
        }

        value = 2 * m++;
        return true;
    });

    FuncProduser<int> oddNumproduser([m = 0](int& value) mutable -> bool {
        if (m >= 1'000'000) {
            return false;
        }

        value = (2 * m++) + 1;
        return true;
    });

    Chan<int> chan(true, 10);
    chan.attachToProduser(evenNumProduser);
    chan.attachToProduser(oddNumproduser);
    evenNumProduser.start();
    oddNumproduser.start();

    int res = count(chan, 100'000);
    GTEST_ASSERT_EQ(res, 100'000);
}

TEST(ChanTest, OneToMany)
{
    FuncProduser<int> numProduser([m = 0](int& value) mutable -> bool {
        if (m >= 1000) {
            return false;
        }

        value = m++;
        return true;
    });

    Chan<int> chan(true, 100);
    numProduser.attachConsumer(chan.makeInput());

    auto thread = std::thread([&chan]() {
        int res = sum(chan);
        GTEST_ASSERT_EQ(res, 499'500);
    });

    Chan<int> chan2(true, 100);
    numProduser.attachConsumer(chan2.makeInput());

    auto thread2 = std::thread([&chan2]() {
        int res = count(chan2);
        GTEST_ASSERT_EQ(res, 1000);
    });

    numProduser.start();

    thread.join();
    thread2.join();
}
