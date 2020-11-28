#include <chrono>
#include <gtest/gtest.h>

#include <nhope/asyncs/ts-queue.h>
#include <nhope/asyncs/thread-executor.h>
#include <thread>

using namespace std::chrono_literals;
using namespace nhope::asyncs;

TEST(QueueTests, WriteWithTimeout)   // NOLINT
{
    constexpr int IterCount = 100;
    constexpr std::size_t capacity = 42;
    constexpr int writeValue = 42;

    TSQueue<int> queue(capacity);

    ThreadExecutor writeThread;
    for (int i = 0; i < IterCount; ++i) {
        writeThread.post([&] {
            queue.write(int(writeValue), 100ms);
        });
    }
    std::this_thread::sleep_for(4ms);
    EXPECT_EQ(queue.size(), capacity);
}

TEST(QueueTests, WriteWithCapacity)   // NOLINT
{
    constexpr int IterCount = 100;
    constexpr std::size_t capacity = 42;
    constexpr int writeValue = 42;

    TSQueue<int> queue(capacity);

    ThreadExecutor writeThread;
    for (int i = 0; i < IterCount; ++i) {
        writeThread.post([&] {
            queue.write(int(writeValue));
        });
    }
    std::this_thread::sleep_for(4ms);
    EXPECT_EQ(queue.size(), capacity);

    int readVal{0};
    while (queue.read(readVal, 200ms)) {
        EXPECT_EQ(readVal, writeValue);
    }
    EXPECT_TRUE(queue.empty());
}

TEST(QueueTests, ReadFor)   // NOLINT
{
    constexpr int IterCount = 100;
    constexpr int writeValue = 42;

    ThreadExecutor thread;

    TSQueue<int> queue;
    int readValue{0};

    for (int i = 0; i < IterCount; ++i) {
        thread.post([&] {
            std::this_thread::sleep_for(2ms);
            queue.write(int(writeValue));
        });
        bool readed = queue.read(readValue, 4ms);

        EXPECT_TRUE(readed);
        EXPECT_EQ(readValue, writeValue);
    }
}

TEST(QueueTests, GenerateAndRead)   // NOLINT
{
    constexpr int IterCount = 100;
    constexpr int writeValue = 42;

    TSQueue<int> queue;

    ThreadExecutor writeThread;
    ThreadExecutor readThread;
    for (int i = 0; i < IterCount; ++i) {
        writeThread.post([&] {
            std::this_thread::sleep_for(4ms);
            queue.write(int(writeValue));
        });
        if (i < (IterCount / 2)) {
            readThread.post([&] {
                auto readed = queue.read();
                EXPECT_EQ(readed.value(), writeValue);
            });
        }
    }

    std::this_thread::sleep_for(std::chrono::seconds(1));

    EXPECT_EQ(queue.size(), IterCount / 2);
    int readVal{0};
    while (queue.read(readVal)) {
        EXPECT_EQ(readVal, writeValue);
        if (queue.empty()) {
            break;
        }
    }
    EXPECT_TRUE(queue.empty());
}