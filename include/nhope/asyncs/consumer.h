#pragma once

namespace nhope::asyncs {

template<typename T>
class Consumer
{
    Consumer(const Consumer&) = delete;
    Consumer& operator=(const Consumer&) = delete;

public:
    enum class Status
    {
        Ok,
        Closed,
    };

public:
    Consumer() = default;
    virtual ~Consumer() = default;

    virtual Status consume(const T& value) = 0;
};

}   // namespace nhope::asyncs
