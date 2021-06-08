#pragma once

#include "nhope/utils/noncopyable.h"

namespace nhope {

template<typename T>
class Consumer : public Noncopyable
{
public:
    enum class Status
    {
        Ok,
        Closed,
    };

    virtual ~Consumer() = default;

    virtual Status consume(const T& value) = 0;
};

}   // namespace nhope
