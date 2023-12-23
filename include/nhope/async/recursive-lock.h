#pragma once

#include <mutex>
#include <optional>

#include "nhope/utils/noncopyable.h"
#include "nhope/utils/stack-storage.h"

namespace nhope {

/**
 * @brief Recursive lock for std::mutex
 *
 * Unlike std::recursive_mutex, it can be used with std::condition_variable
 * and can be unlocked in a internal call.
 * For example
 * @code
 * 
 * std::mutex m;
 *
 * void a() {
 *     RecursiveLock lock(m);
 *     EXPECT_TRUE(lock.ownsLock());
 *     b();
 *     EXPECT_TRUE(lock.ownsLock()); // Lock was restored after existing from the b
 * }
 *
 * void b() {
 *    RecursiveLock lock(m); // Use exist lock
 *    EXPECT_TRUE(lock.ownsLock());
 *    lock.unlock();
 *    EXPECT_FALSE(lock.ownsLock());
 * }
 * @endcode
 * 
 */
class RecursiveLock final : Noncopyable
{
public:
    /**
     * @brief Saves current state and locks the mutex
     */
    explicit RecursiveLock(std::mutex& mutex)
      : m_lock(LockStorage::get(&mutex))
    {
        if (m_lock == nullptr) {
            m_lock = this->makeLockRec(mutex);
        }

        if (!m_lock->owns_lock()) {
            m_wasLocked = false;
            m_lock->lock();
        }
    }

    /**
     * @brief Restores the previous state the mutex
     */
    ~RecursiveLock()
    {
        if (m_wasLocked != m_lock->owns_lock()) {
            if (m_wasLocked) {
                m_lock->lock();
            } else {
                m_lock->unlock();
            }
        }
    }

    [[nodiscard]] bool isFirst() const noexcept
    {
        return m_lockRec.has_value();
    }

    void lock()
    {
        m_lock->lock();
    }

    void unlock()
    {
        m_lock->unlock();
    }

    [[nodiscard]] bool ownsLock() const noexcept
    {
        return m_lock->owns_lock();
    }

    operator std::unique_lock<std::mutex>&() noexcept
    {
        return *m_lock;
    }

private:
    using LockStorage = StackStorage<std::mutex*, std::unique_lock<std::mutex>>;
    using LockRec = LockStorage::Record;

    std::unique_lock<std::mutex>* makeLockRec(std::mutex& mutex) noexcept
    {
        m_lockRec.emplace(&mutex, std::unique_lock(mutex, std::defer_lock));
        return &m_lockRec->value();
    }

    std::optional<LockRec> m_lockRec;
    std::unique_lock<std::mutex>* m_lock;
    bool m_wasLocked = true;
};

}   // namespace nhope
