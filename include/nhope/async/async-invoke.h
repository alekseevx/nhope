#pragma once

#include <functional>
#include <utility>

#include <nhope/async/future.h>
#include <nhope/async/ao-context.h>

namespace nhope {

template<class Executor, typename Fn, typename... Args>
auto asyncInvoke(BaseAOContext<Executor>& aoCtx, Fn&& fn, Args&&... args)
{
    auto bindedFn = std::bind(std::forward<Fn>(fn), std::forward<Args>(args)...);
    return makeReadyFuture().then(aoCtx, std::move(bindedFn));
}

template<class Executor, typename Fn, typename... Args>
auto invoke(BaseAOContext<Executor>& aoCtx, Fn&& fn, Args&&... args)
{
    auto future = asyncInvoke(aoCtx, std::forward<Fn>(fn), std::forward<Args>(args)...);
    return future.get();
}

}   // namespace nhope
