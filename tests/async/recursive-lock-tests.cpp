#include <mutex>

#include <gtest/gtest.h>

#include "nhope/async/recursive-lock.h"

using namespace nhope;

TEST(RecursiveLock, makeLock)   // NOLINT
{
    std::mutex m;

    {
        RecursiveLock lock(m);
        EXPECT_TRUE(lock.ownsLock());
        EXPECT_TRUE(lock.isFirst());

        std::unique_lock uniqueLock(m, std::defer_lock);
        EXPECT_FALSE(uniqueLock.try_lock());
    }

    std::unique_lock uniqueLock(m, std::defer_lock);
    EXPECT_TRUE(uniqueLock.try_lock());
}

TEST(RecursiveLock, restoreLock)   // NOLINT
{
    std::mutex m;

    RecursiveLock lock(m);

    EXPECT_TRUE(lock.isFirst());
    EXPECT_TRUE(lock.ownsLock());
    {
        RecursiveLock lock2(m);
        EXPECT_FALSE(lock2.isFirst());
        EXPECT_TRUE(lock2.ownsLock());
        lock2.unlock();
        EXPECT_FALSE(lock2.ownsLock());
    }
    EXPECT_TRUE(lock.ownsLock());
}

TEST(RecursiveLock, restoreUnlock)   // NOLINT
{
    std::mutex m;

    RecursiveLock lock(m);
    lock.unlock();

    EXPECT_TRUE(lock.isFirst());
    EXPECT_FALSE(lock.ownsLock());
    {
        RecursiveLock lock2(m);
        EXPECT_FALSE(lock2.isFirst());
        EXPECT_TRUE(lock2.ownsLock());
    }
    EXPECT_FALSE(lock.ownsLock());
}
