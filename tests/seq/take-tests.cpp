#include <nhope/async/ao-context.h>
#include <nhope/async/future.h>
#include <nhope/seq/func-producer.h>
#include <nhope/seq/take.h>
#include "nhope/seq/detail/take-one-consumer.h"

#include <gtest/gtest.h>

using namespace nhope;

TEST(TakeOneFromProducer, Take)   // NOLINT
{
    constexpr int startValue = 0;

    FuncProducer<int> numProducer([m = startValue](int& value) mutable {
        value = m++;
        return true;
    });

    Future<int> future = takeOne(numProducer);
    EXPECT_FALSE(future.isReady());

    numProducer.start();

    EXPECT_EQ(future.get(), 0);
}

TEST(TakeOneFromProducer, DestroyProducer)   // NOLINT
{
    constexpr int startValue = 0;

    Future<int> future;

    {
        FuncProducer<int> numProducer([m = startValue](int& value) mutable {
            value = m++;
            return true;
        });

        future = takeOne(numProducer);
    }

    EXPECT_THROW(future.get(), AsyncOperationWasCancelled);   // NOLINT
}

TEST(TakeOneConsumer, Take2)   // NOLINT
{
    detail::TakeOneConsumer<int> consumer;
    int x{};
    EXPECT_EQ(consumer.consume(x), Consumer<int>::Status::Closed);
    EXPECT_EQ(consumer.consume(x), Consumer<int>::Status::Closed);
}