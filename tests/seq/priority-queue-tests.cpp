#include <optional>

#include <gtest/gtest.h>

#include "nhope/seq/priority-queue.h"

TEST(PriorityQueue, Simple)   //NOLINT
{
    nhope::PriorityQueue<int> queue;
    ASSERT_TRUE(queue.empty());

    ASSERT_EQ(queue.pop(), std::nullopt);

    ASSERT_EQ(queue.size(), 0);
    queue.push(1);
    ASSERT_EQ(queue.size(), 1);
    queue.push(2);
    ASSERT_EQ(queue.size(), 2);

    ASSERT_EQ(queue.pop(), 1);
    ASSERT_EQ(queue.pop(), 2);
    ASSERT_TRUE(queue.empty());
}

TEST(PriorityQueue, Priority)   //NOLINT
{
    nhope::PriorityQueue<int> queue;
    queue.push(0);
    queue.push(1);
    queue.push(2, 1);
    queue.push(3, 1);
    queue.push(4);

    ASSERT_EQ(queue.pop(), 2);
    ASSERT_EQ(queue.pop(), 3);
    ASSERT_EQ(queue.pop(), 0);
    ASSERT_EQ(queue.pop(), 1);
    ASSERT_EQ(queue.pop(), 4);
}

TEST(PriorityQueue, EraseAndClear)   //NOLINT
{
    nhope::PriorityQueue<int> queue;
    queue.push(0);
    queue.push(1);
    queue.push(2, 1);
    queue.push(3, 1);
    queue.push(4);

    ASSERT_EQ(queue.pop(), 2);
    ASSERT_EQ(queue.pop(), 3);
    ASSERT_EQ(queue.pop(), 0);
    queue.remove_if([](const int& v, int /*unused*/) {
        return v == 1;
    });

    ASSERT_EQ(queue.pop(), 4);

    queue.clear();
    ASSERT_EQ(queue.pop(), std::nullopt);

    queue.push(1);
    queue.push(1);
    queue.remove_if([](const int& v, int /*unused*/) {
        return v == 1;
    });
    ASSERT_TRUE(queue.empty());
}
