#pragma once

#include <memory>
#include "consumer.h"
#include "nhope/utils/noncopyable.h"

namespace nhope {

template<typename T>
class Producer : public Noncopyable
{
public:
    virtual ~Producer() = default;

    virtual void attachConsumer(std::unique_ptr<Consumer<T>> consumer) = 0;
};

}   // namespace nhope
