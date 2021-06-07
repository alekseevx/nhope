#pragma once

#include <exception>

namespace nhope {

template<typename Fn>
class ScopeExit
{
    Fn m_fn;

public:
    explicit ScopeExit(Fn&& f)
      : m_fn(std::forward<Fn>(f))
    {}

    ~ScopeExit()
    {
        try {
            m_fn();
        } catch (...) {
            // TODO: Logging
        }
    }
};

}   // namespace nhope
