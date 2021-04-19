#pragma once

#include <functional>
#include <utility>

#include <nhope/async/future.h>
#include <nhope/async/ao-context.h>

namespace nhope {

template<typename Fn, typename... Args>
auto asyncInvoke(AOContext& aoCtx, Fn&& fn, Args&&... args)
{
    auto bindedFn = std::bind(std::forward<Fn>(fn), std::forward<Args>(args)...);
    return makeReadyFuture().then(aoCtx, std::move(bindedFn));
}

template<typename Fn, typename... Args>
auto invoke(AOContext& aoCtx, Fn&& fn, Args&&... args)
{
    auto future = asyncInvoke(aoCtx, std::forward<Fn>(fn), std::forward<Args>(args)...);
    return future.get();
}

}   // namespace nhope
