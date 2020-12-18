#pragma once

#include <functional>
#include <utility>

#include "nhope/asyncs/future.h"

namespace nhope::asyncs {

class AOContext;

template<typename Fn, typename... Args>
auto asyncInvoke(AOContext& aoCtx, Fn&& fn, Args&&... args)
{
    auto bindedFn = std::bind(std::forward<Fn>(fn), std::forward<Args>(args)...);
    return makeReadyFuture<void>().thenValue(aoCtx, std::move(bindedFn));
}

template<typename Fn, typename... Args>
auto invoke(AOContext& aoCtx, Fn&& fn, Args&&... args)
{
    auto future = asyncInvoke(aoCtx, std::forward<Fn>(fn), std::forward<Args>(args)...);
    return future.get();
}

}   // namespace nhope::asyncs
