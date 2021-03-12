#pragma once

namespace nhope {

class Noncopyable
{
public:
    constexpr Noncopyable() = default;

    Noncopyable(const Noncopyable&) = delete;
    Noncopyable& operator=(const Noncopyable&) = delete;
};

}   // namespace nhope
