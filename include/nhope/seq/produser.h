#pragma once

#include <memory>
#include "consumer.h"
#include "nhope/utils/noncopyable.h"

namespace nhope {

template<typename T>
class Produser : public Noncopyable
{
public:
    virtual ~Produser() = default;

    virtual void attachConsumer(std::unique_ptr<Consumer<T>> consumer) = 0;
};

}   // namespace nhope
