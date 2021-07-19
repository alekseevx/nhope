#include "nhope/async/event.h"
#include <mutex>

namespace nhope {

Event::Event() = default;

Event::~Event() = default;

void Event::set()
{
    std::scoped_lock lock(m_mutex);
    if (m_signaled) {
        return;
    }

    m_signaled = true;
    m_cv.notify_all();
}

void Event::wait()
{
    std::unique_lock lock(m_mutex);
    m_cv.wait(lock, [this] {
        return m_signaled;
    });
}

bool Event::waitFor(std::chrono::nanoseconds timeout)
{
    std::unique_lock lock(m_mutex);
    return m_cv.wait_for(lock, timeout, [this] {
        return m_signaled;
    });
}

}   // namespace nhope
