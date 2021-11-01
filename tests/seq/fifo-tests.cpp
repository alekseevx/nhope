#include <array>
#include <optional>

#include <gtest/gtest.h>

#include "gsl/span"

#include "nhope/seq/fifo.h"
#include "nhope/utils/noncopyable.h"

namespace {

using namespace nhope;
using namespace gsl;
constexpr auto etalonData = std::array{1, 2, 3, 4, 5, 6, 7, 8, 9, 10};

}   // namespace

TEST(Fifo, push)   // NOLINT
{
    Fifo<int, 4> fifo;
    EXPECT_TRUE(fifo.empty());
    EXPECT_EQ(fifo.push(span(etalonData).first<2>()), 2);
    EXPECT_EQ(fifo.size(), 2);
    EXPECT_EQ(fifo.push(span(etalonData).subspan(2)), 2);
    EXPECT_EQ(fifo.size(), 4);
}

TEST(Fifo, pop)   // NOLINT
{
    Fifo<int, 4> fifo;
    std::array<int, 4> test{};

    EXPECT_EQ(fifo.push(etalonData), 4);
    EXPECT_EQ(fifo.size(), 4);
    EXPECT_EQ(fifo.pop(), 1);
    EXPECT_EQ(fifo.pop(), 2);
    EXPECT_EQ(fifo.size(), 2);
    EXPECT_EQ(fifo.pop(test), 2);
    EXPECT_EQ(test[0], 3);
    EXPECT_EQ(test[1], 4);
    EXPECT_TRUE(fifo.empty());
    EXPECT_EQ(fifo.pop(), std::nullopt);
}

TEST(Fifo, overflow)   // NOLINT
{
    Fifo<int, 4> fifo;
    constexpr auto testCount{10};
    std::array<int, testCount> test{};

    EXPECT_EQ(fifo.push(etalonData), 4);
    EXPECT_EQ(fifo.size(), 4);
    fifo.pop(span(test).first<2>());
    EXPECT_EQ(fifo.size(), 2);

    EXPECT_EQ(test[0], 1);
    EXPECT_EQ(test[1], 2);
    fifo.pop(span(test).subspan(2, 2));
    EXPECT_EQ(test[2], 3);
    EXPECT_EQ(test[3], 4);
    EXPECT_EQ(fifo.pop(), std::nullopt);

    EXPECT_EQ(fifo.size(), 0);
    EXPECT_TRUE(fifo.empty());

    EXPECT_EQ(fifo.push(span(etalonData).subspan(4, 2)), 2);
    EXPECT_EQ(fifo.size(), 2);
    EXPECT_EQ(fifo.push(span(etalonData).subspan(6)), 2);
    EXPECT_EQ(fifo.size(), 4);

    EXPECT_EQ(fifo.pop(span(test).subspan(4)), 4);
    EXPECT_EQ(fifo.size(), 0);
}
