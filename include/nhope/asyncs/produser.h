#pragma once

#include <stddef.h>
#include "consumer.h"

namespace nhope::asyncs {

template<typename T>
class Produser
{
    Produser(const Produser&) = delete;
    Produser& operator=(const Produser&) = delete;

public:
    Produser() = default;
    virtual ~Produser() = default;

    virtual void attachConsumer(std::unique_ptr<Consumer<T>> consumer) = 0;
};

}   // namespace nhope::asyncs
