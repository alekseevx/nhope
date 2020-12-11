#pragma once

#include <functional>
#include <memory>
#include <type_traits>
#include <utility>

#include <boost/exception_ptr.hpp>
#include <boost/thread/future.hpp>

#include "nhope/asyncs/ao-context.h"
#include "nhope/asyncs/thread-executor.h"

namespace nhope::asyncs {

template<typename Fn, typename... Args>
using AsyncInvokeResult = boost::future<std::invoke_result_t<Fn, Args...>>;

template<typename Fn, typename... Args>
AsyncInvokeResult<Fn, Args...> asyncInvoke(AOContext& aoCtx, Fn&& fn, Args&&... args)
{
    using Result = std::invoke_result_t<Fn, Args...>;

    auto& executor = aoCtx.executor();
    auto promise = std::make_shared<boost::promise<Result>>();
    auto func = std::bind(std::forward<Fn>(fn), std::forward<Args>(args)...);

    std::function invokeFunc = [promise, func = std::move(func)] {
        try {
            if constexpr (std::is_void_v<Result>) {
                func();
                promise->set_value();
            } else {
                promise->set_value(func());
            }
        } catch (...) {
            auto exPtr = boost::current_exception();
            promise->set_exception(exPtr);
        }
    };
    std::function cancel = [promise] {
        promise->set_exception(AsyncOperationWasCancelled());
    };

    executor.post(aoCtx.newAsyncOperation(std::move(invokeFunc), std::move(cancel)));

    return promise->get_future();
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
