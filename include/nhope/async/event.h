#pragma once

#include <chrono>
#include <condition_variable>
#include <mutex>

#include "nhope/utils/noncopyable.h"

namespace nhope {

class Event final : Noncopyable
{
public:
    Event();
    ~Event();

    void set();

    void wait();
    bool waitFor(std::chrono::nanoseconds timeout);

private:
    std::mutex m_mutex;
    std::condition_variable m_cv;
    bool m_signaled = false;
};

}   // namespace nhope
