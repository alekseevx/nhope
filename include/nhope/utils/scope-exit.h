#pragma once

#include <exception>

#include "noncopyable.h"
#include "type.h"

namespace nhope {

template<typename Fn>
class ScopeExit : public Noncopyable
{
    Fn m_fn;

public:
    static_assert(checkFunctionSignatureV<Fn, void>, "expect void() signature");

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
