#pragma once

#include <memory>
#include <utility>

#include "nhope/async/future.h"
#include "producer.h"

#include "detail/take-one-consumer.h"

namespace nhope {

template<typename T>
Future<T> takeOne(Producer<T>& producer)
{
    auto consumer = std::make_unique<detail::TakeOneConsumer<T>>();
    auto future = consumer->future();

    producer.attachConsumer(std::move(consumer));

    return future;
}

}   // namespace nhope
