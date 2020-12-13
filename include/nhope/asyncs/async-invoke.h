#pragma once

#include <exception>
#include <functional>
#include <memory>
#include <type_traits>
#include <utility>

#include "nhope/asyncs/ao-context.h"
#include "nhope/asyncs/future.h"
#include "nhope/asyncs/thread-executor.h"

namespace nhope::asyncs {

template<typename Fn, typename... Args>
using AsyncInvokeResult = Future<std::invoke_result_t<Fn, Args...>>;

template<typename Fn, typename... Args>
AsyncInvokeResult<Fn, Args...> asyncInvoke(AOContext& aoCtx, Fn&& fn, Args&&... args)
{
    using Result = std::invoke_result_t<Fn, Args...>;

    auto& executor = aoCtx.executor();
    auto promise = std::make_shared<Promise<Result>>();
    auto func = std::bind(std::forward<Fn>(fn), std::forward<Args>(args)...);

    std::function invokeFunc = [promise, func = std::move(func)] {
        try {
            if constexpr (std::is_void_v<Result>) {
                func();
                promise->setValue();
            } else {
                promise->setValue(func());
            }
        } catch (...) {
            auto exPtr = std::current_exception();
            promise->setException(exPtr);
        }
    };
    std::function cancel = [promise] {
        auto ex = std::make_exception_ptr(AsyncOperationWasCancelled());
        promise->setException(ex);
    };

    executor.post(aoCtx.newAsyncOperation(std::move(invokeFunc), std::move(cancel)));

    return promise->future();
}

template<typename Fn, typename... Args>
std::invoke_result_t<Fn, Args...> invoke(AOContext& aoCtx, Fn&& fn, Args&&... args)
{
    auto future = asyncInvoke(aoCtx, std::forward<Fn>(fn), std::forward<Args>(args)...);
    if constexpr (std::is_void_v<std::invoke_result_t<Fn, Args...>>) {
        future.get();
        return;
    } else {
        return future.get();
    }
}

}   // namespace nhope::asyncs
