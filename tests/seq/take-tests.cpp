#include <nhope/async/ao-context.h>
#include <nhope/async/future.h>
#include <nhope/seq/func-produser.h>
#include <nhope/seq/take.h>

#include <gtest/gtest.h>
#include <thread>

using namespace nhope;

TEST(TakeOneFromProduser, Take)   // NOLINT
{
    constexpr int startValue = 0;

    FuncProduser<int> numProduser([m = startValue](int& value) mutable {
        value = m++;
        return true;
    });

    Future<int> future = takeOne(numProduser);
    EXPECT_EQ(future.state(), FutureState::waiting);

    numProduser.start();

    EXPECT_EQ(future.get(), 0);
}

TEST(TakeOneFromProduser, DestroyProduser)   // NOLINT
{
    constexpr int startValue = 0;

    Future<int> future;

    {
        FuncProduser<int> numProduser([m = startValue](int& value) mutable {
            value = m++;
            return true;
        });

        future = takeOne(numProduser);
    }

    EXPECT_THROW(future.get(), AsyncOperationWasCancelled);   // NOLINT
}
