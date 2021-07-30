#include <functional>
#include <gtest/gtest.h>

#include "nhope/utils/stack-storage.h"

using StackStorage = nhope::StackStorage<int, int>;

TEST(StackStorage, empty)   // NOLINT
{
    EXPECT_FALSE(StackStorage::contains(0));
    EXPECT_EQ(StackStorage::get(0), nullptr);
}

TEST(StackStorage, pushPopRecs)   // NOLINT
{
    constexpr int maxDepth = 10;

    auto checkRecs = [](int depth)   // NOLINT(readability-function-cognitive-complexity)
    {
        for (int key = 0; key <= depth; key++) {
            EXPECT_TRUE(StackStorage::contains(key));
            const int* value = StackStorage::get(key);
            EXPECT_NE(value, nullptr);
            EXPECT_EQ(*value, key);
        }

        EXPECT_FALSE(StackStorage::contains(depth + 1));
        EXPECT_EQ(StackStorage::get(depth + 1), nullptr);
    };

    std::function<void(int)> testFn = [&](int depth) {
        StackStorage::Record rec(depth, depth);

        checkRecs(depth);
        if (depth < maxDepth) {
            testFn(depth + 1);
            checkRecs(depth);
        }
    };
}

TEST(StackStorage, overrideKey)   // NOLINT
{
    StackStorage::Record rec(0, 0);
    {
        StackStorage::Record rec2(0, 1);
        EXPECT_EQ(*StackStorage::get(0), 1);
    }
    EXPECT_EQ(*StackStorage::get(0), 0);
}
