#pragma once

#include <memory>
#include <utility>

#include "../async/future.h"
#include "produser.h"

#include "detail/take-one-consumer.h"

namespace nhope {

template<typename T>
Future<T> takeOne(Produser<T>& produser)
{
    auto consumer = std::make_unique<detail::TakeOneConsumer<T>>();
    auto future = consumer->future();

    produser.attachConsumer(std::move(consumer));

    return future;
}

}   // namespace nhope
