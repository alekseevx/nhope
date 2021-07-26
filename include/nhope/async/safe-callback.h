#pragma once

#include <functional>
#include <memory>
#include <utility>

#include "nhope/async/ao-context.h"
#include "nhope/async/detail/safe-callback.h"

namespace nhope {

/**
 * @brief Функция для создания безопасного callback-а
 *
 * Безопасный callback обладает следующими свойствами:
 * - Безопасный callback можно вызывать из любого потока, исходный callback
 * будет асинхронно вызван в executor-е AOContext-а.
 * - Безопасный callback может быть вызван после уничтожения AOContext-а. В этом
 * случае будет выброшено исключение #AOContextClosed.
 *
 * @retval Безопасный callback
 */
template<typename... Args>
std::function<void(Args...)> makeSafeCallback(AOContext& aoCtx, std::function<void(Args...)> callback)
{
    return [aoCtx = AOContextWeekRef(aoCtx), callback = std::move(callback)](Args... args) mutable {
        auto aoHandler = detail::makeSafeCallbackAsyncOperationHandler([callback, args...] {
            callback(args...);
        });

        auto callAOHandler = aoCtx.putAOHandler(std::move(aoHandler));
        callAOHandler();
    };
}

}   // namespace nhope