#include <gtest/gtest.h>

#include <memory>
#include <nhope/seq/weak_list.h>

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
