#pragma once

#include <functional>
#include <type_traits>
#include <utility>

#include "nhope/async/ao-context.h"
#include "nhope/async/detail/async-invoke-aohandler.h"
#include "nhope/async/future.h"

namespace nhope {

template<typename Fn, typename... Args>
auto asyncInvoke(AOContext& aoCtx, Fn&& fn, Args&&... args)
{
    using Result = std::invoke_result_t<Fn, Args...>;

    Promise<Result> promise;
    auto future = promise.future();

    auto aoHandler = detail::makeAsyncInvokeAOHandler(std::move(promise),
                                                      std::bind(std::forward<Fn>(fn), std::forward<Args>(args)...));
    aoCtx.callAOHandler(std::move(aoHandler));

    return future.unwrap();
}

template<typename Fn, typename... Args>
auto invoke(AOContext& aoCtx, Fn&& fn, Args&&... args)
{
    if (aoCtx.workInThisThread()) {
        return std::invoke(std::forward<Fn>(fn), std::forward<Args>(args)...);
    }

    auto future = asyncInvoke(aoCtx, std::forward<Fn>(fn), std::forward<Args>(args)...);
    return future.get();
}

}   // namespace nhope
