#pragma once

namespace nhope {

template<typename T>
class Consumer
{
public:
    enum class Status
    {
        Ok,
        Closed,
    };

    Consumer(const Consumer&) = delete;
    Consumer& operator=(const Consumer&) = delete;
    Consumer() = default;
    virtual ~Consumer() = default;

    virtual Status consume(const T& value) = 0;
};

}   // namespace nhope
