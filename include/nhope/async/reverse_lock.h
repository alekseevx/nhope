#pragma once

namespace nhope {

/* Borrowed from git@github.com:bsazonov/rethread.git */
template<typename Lock>
class ReverseLock final
{
public:
    explicit ReverseLock(Lock& lock)
      : m_lock(lock)
    {
        m_lock.unlock();
    }

    ~ReverseLock()
    {
        m_lock.lock();
    }

private:
    Lock& m_lock;
};

}   // namespace nhope
