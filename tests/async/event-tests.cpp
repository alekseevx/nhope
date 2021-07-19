#include <atomic>
#include <chrono>
#include <memory>
#include <thread>

#include <gtest/gtest.h>

#include "nhope/async/event.h"

using namespace std::literals;

TEST(Event, wait)   // NOLINT
{
    constexpr int iterCount = 10;

    std::atomic<bool> flag{};
    for (int i = 0; i < iterCount; ++i) {
        flag = false;

        nhope::Event event;
        std::thread([&, i] {
            std::this_thread::sleep_for(std::chrono::milliseconds(i));
            flag = true;
            event.set();
        }).detach();

        event.wait();
        EXPECT_TRUE(flag);

        EXPECT_NO_THROW(event.set());   // NOLINT
    }
}

TEST(Event, waitFor)   // NOLINT
{
    constexpr int iterCount = 10;

    std::atomic<bool> flag{};
    for (int i = 0; i < iterCount; ++i) {
        flag = false;

        nhope::Event event;

        EXPECT_FALSE(event.waitFor(1ms));

        std::thread([&, i] {
            std::this_thread::sleep_for(std::chrono::milliseconds(i));
            flag = true;
            event.set();
        }).detach();

        EXPECT_TRUE(event.waitFor(1s));
        EXPECT_TRUE(flag);
    }
}

TEST(Event, repeatSet)   // NOLINT
{
    nhope::Event event;
    event.set();
    event.set();   // NOLINT
    event.wait();
}
