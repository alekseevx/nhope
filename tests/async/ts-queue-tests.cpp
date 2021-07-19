#include <chrono>
#include <thread>

#include "nhope/async/ts-queue.h"
#include "nhope/async/thread-executor.h"
#include "nhope/async/event.h"
#include <gtest/gtest.h>

using namespace std::chrono_literals;
using namespace nhope;

TEST(TSQueue, OneWriterManyReaders)   // NOLINT
{
    constexpr int iterCount = 100;
    constexpr int writeValue = 42;

    TSQueue<int> queue;

    ThreadExecutor writeThread;
    ThreadExecutor readThread1;
    ThreadExecutor readThread2;
    ThreadExecutor readThread3;
    readThread1.post([&] {
        while (auto read = queue.read()) {
            EXPECT_EQ(read.value(), writeValue);
        }
    });
    readThread2.post([&] {
        while (auto read = queue.read()) {
            EXPECT_EQ(read.value(), writeValue);
        }
    });
    readThread3.post([&] {
        while (auto read = queue.read()) {
            EXPECT_EQ(read.value(), writeValue);
        }
    });
    for (int i = 0; i < iterCount; ++i) {
        writeThread.post([&] {
            queue.write(int(writeValue));
        });
    }
    std::this_thread::sleep_for(300ms);

    EXPECT_TRUE(queue.empty());
    queue.close();
}

TEST(TSQueue, CloseQueue)   // NOLINT
{
    constexpr int iterCount = 100;
    constexpr int closeIter = 42;

    TSQueue<int> queue;

    for (int i = 0; i < iterCount; ++i) {
        if (i == closeIter) {
            queue.close();
        }
        queue.write(int(closeIter));
    }
    EXPECT_EQ(queue.size(), closeIter);
}

TEST(TSQueue, WriteWithTimeout)   // NOLINT
{
    constexpr int iterCount = 100;
    constexpr std::size_t capacity = 42;
    constexpr int writeValue = 42;

    TSQueue<int> queue(capacity);

    ThreadExecutor writeThread;
    for (int i = 0; i < iterCount; ++i) {
        writeThread.post([&] {
            queue.write(writeValue, 100ms);
        });
    }
    std::this_thread::sleep_for(4ms);
    EXPECT_EQ(queue.size(), capacity);
}

TEST(TSQueue, WriteWithCapacity)   // NOLINT
{
    constexpr int iterCount = 100;
    constexpr std::size_t capacity = 42;
    constexpr int writeValue = 42;

    TSQueue<int> queue(capacity);

    ThreadExecutor writeThread;
    for (int i = 0; i < iterCount; ++i) {
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

TEST(TSQueue, ReadFor)   // NOLINT
{
    constexpr int iterCount = 100;
    constexpr int writeValue = 42;

    ThreadExecutor thread;

    TSQueue<int> queue;
    int readValue{0};

    for (int i = 0; i < iterCount; ++i) {
        thread.post([&] {
            std::this_thread::sleep_for(2ms);
            queue.write(int(writeValue));
        });
        bool read = queue.read(readValue, 50ms);

        EXPECT_TRUE(read);
        EXPECT_EQ(readValue, writeValue);
    }
}

TEST(TSQueue, GenerateAndRead)   // NOLINT
{
    constexpr int iterCount = 100;
    constexpr int writeValue = 42;

    TSQueue<int> queue;

    Event writterFinishedEvent;
    Event readerFinishedEvent;

    ThreadExecutor writeThread;
    ThreadExecutor readThread;
    for (int i = 0; i < iterCount; ++i) {
        writeThread.post([&] {
            std::this_thread::sleep_for(4ms);
            queue.write(int(writeValue));
        });
        if (i < (iterCount / 2)) {
            readThread.post([&] {
                auto read = queue.read();
                EXPECT_EQ(read.value(), writeValue);
            });
        }
    }

    writeThread.post([&] {
        writterFinishedEvent.set();
    });
    readThread.post([&] {
        readerFinishedEvent.set();
    });

    writterFinishedEvent.wait();
    readerFinishedEvent.wait();

    EXPECT_EQ(queue.size(), iterCount / 2);
    int readVal{0};
    while (queue.read(readVal)) {
        EXPECT_EQ(readVal, writeValue);
        if (queue.empty()) {
            break;
        }
    }
    EXPECT_TRUE(queue.empty());
}