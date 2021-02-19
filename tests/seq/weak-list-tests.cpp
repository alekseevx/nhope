#include <atomic>
#include <memory>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

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
    EXPECT_EQ(counter, size);
    weak.clearExpired();
    ASSERT_EQ(size, weak.size());
    temp.clear();

    for (auto x : weak) {
        FAIL() << "list expired ";
    }
    weak.clearExpired();
    ASSERT_TRUE(weak.empty());
}

TEST(TSWeakList, sharedList)   //NOLINT
{
    static constexpr int size = 1000;

    TSWeakList<int> weak;
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
    EXPECT_EQ(counter, size);
    weak.clearExpired();
    ASSERT_EQ(size, weak.size());
    temp.clear();

    for (auto x : weak) {
        FAIL() << "list expired ";
    }
    weak.clearExpired();
    ASSERT_TRUE(weak.empty());
}

TEST(TSWeakList, ThreadRace)   //NOLINT
{
    static constexpr int size = 1000;
    std::atomic_bool finished{false};

    TSWeakList<int> weak;
    EXPECT_TRUE(weak.empty());

    std::vector<std::shared_ptr<int>> temp;
    temp.reserve(size);

    for (size_t i = 0; i < size; i++) {
        auto t = std::make_shared<int>(i);
        temp.emplace_back(t);
    }

    auto t1 = std::thread([&] {
        for (size_t i = 0; i < size / 2; i++) {
            weak.emplace_back(temp[i]);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    });

    auto t2 = std::thread([&] {
        for (size_t i = size / 2; i < size; i++) {
            weak.emplace_back(temp[i]);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    });

    int counter{};

    auto t3 = std::thread([&] {
        while (!finished) {
            for (auto x : weak) {
                counter++;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(4));
        }
    });

    t1.join();
    t2.join();

    finished = true;

    ASSERT_EQ(size, weak.size());
    temp.clear();

    t3.join();
    EXPECT_GE(counter, size);

    for (auto x : weak) {
        FAIL() << "list expired ";
    }
    weak.clearExpired();
    ASSERT_TRUE(weak.empty());
}

TEST(TSWeakList, ForEach)   //NOLINT
{
    static constexpr int size = 1000;
    std::atomic_bool finished{false};

    TSWeakList<int> weak;
    EXPECT_TRUE(weak.empty());

    std::vector<std::shared_ptr<int>> temp;
    temp.reserve(size);

    for (size_t i = 0; i < size; i++) {
        auto t = std::make_shared<int>(i);
        temp.emplace_back(t);
    }

    auto t1 = std::thread([&] {
        for (size_t i = 0; i < size; i++) {
            weak.emplace_back(temp[i]);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    });

    int counter{};

    auto t2 = std::thread([&] {
        while (!finished) {
            weak.forEach([&](const std::shared_ptr<int>& val) {
                counter += *val;

                // deadlock is imposible
                EXPECT_FALSE(weak.empty());
            });
            std::this_thread::sleep_for(std::chrono::milliseconds(4));
        }
    });

    t1.join();

    finished = true;

    ASSERT_EQ(size, weak.size());
    temp.clear();

    t2.join();

    EXPECT_GE(counter, size);

    for (auto x : weak) {
        FAIL() << "list expired ";
    }
    weak.clearExpired();
    ASSERT_TRUE(weak.empty());
}