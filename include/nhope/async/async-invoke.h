#pragma once

#include <functional>
#include <type_traits>
#include <utility>

#include "nhope/async/ao-context.h"
#include "nhope/async/future.h"

namespace nhope {

template<typename Fn, typename... Args>
auto asyncInvoke(AOContext& aoCtx, Fn&& fn, Args&&... args)
{
    return makeReadyFuture().then(aoCtx, std::bind(std::forward<Fn>(fn), std::forward<Args>(args)...));
}

template<typename Fn, typename... Args>
auto invoke(AOContext& aoCtx, Fn&& fn, Args&&... args)
{
    if (aoCtx.workInThisThread()) {
        throw DetectedDeadlock();
    }

    auto future = asyncInvoke(aoCtx, std::forward<Fn>(fn), std::forward<Args>(args)...);
    return future.get();
}

}   // namespace nhope
