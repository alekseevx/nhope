#include <gtest/gtest.h>

#include <memory>
#include <nhope/seq/weak_list.h>
#include <vector>

namespace {
using namespace nhope;

}   // namespace

TEST(WeakList, simpleList)   //NOLINT
{
    static constexpr int size = 1000;

    WeakList<int> weak;
    EXPECT_TRUE(weak.empty());

    for (size_t i = 0; i < size; i++) {
        weak.emplace_back(std::make_shared<int>(i));
    }

    for (auto x : weak) {
        FAIL() << "list expired ";
    }

    ASSERT_EQ(size, weak.size());

    weak.clearExpired();

    ASSERT_TRUE(weak.empty());
}

TEST(WeakList, sharedList)   //NOLINT
{
    static constexpr int size = 1000;

    WeakList<int> weak;
    EXPECT_TRUE(weak.empty());

    std::vector<std::shared_ptr<int>> temp;
    temp.reserve(size);

    for (size_t i = 0; i < size; i++) {
        auto t = std::make_shared<int>(i);
        temp.emplace_back(t);
        weak.emplace_back(t);
    }

    int counter = 0;
    for (auto x : weak) {
        counter++;
    }

    ASSERT_EQ(size, weak.size());
    temp.clear();
    weak.clearExpired();

    EXPECT_EQ(counter, size);

    for (auto x : weak) {
        FAIL() << "list expired ";
    }

    ASSERT_TRUE(weak.empty());
}
