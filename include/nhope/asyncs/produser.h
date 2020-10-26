#pragma once

#include <memory>
#include "consumer.h"

namespace nhope::asyncs {

template<typename T>
class Produser
{
public:
    Produser(const Produser&) = delete;
    Produser& operator=(const Produser&) = delete;

    Produser() = default;
    virtual ~Produser() = default;

    virtual void attachConsumer(std::unique_ptr<Consumer<T>> consumer) = 0;
};

}   // namespace nhope::asyncs
