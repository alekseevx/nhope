#pragma once

#include <atomic>

namespace nhope {

namespace detail {
class AOContextImpl;
}

class AOContextCloseHandler
{
    friend class detail::AOContextImpl;

public:
    virtual ~AOContextCloseHandler() = default;
    virtual void aoContextClose() noexcept = 0;

private:
    AOContextCloseHandler* m_prev = nullptr;
    AOContextCloseHandler* m_next = nullptr;
    bool* m_destroyed = nullptr;
    std::atomic<bool> m_done = false;
};

}   // namespace nhope
