#pragma once

#include <functional>
#include <memory>
#include <utility>

#include "nhope/async/ao-context.h"
#include "nhope/async/detail/safe-callback-aohandler.h"

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
    // https://gitlab.olimp.lan/alekseev/nhope/-/issues/8
    auto callbackPtr = std::make_shared<std::function<void(Args...)>>(std::move(callback));

    return [aoCtx = AOContextWeekRef(aoCtx), callbackPtr](Args... args) mutable {
        auto aoHandler = detail::makeSafeCallbackAOHandler([callbackPtr, args...] {
            (*callbackPtr)(args...);
        });

        aoCtx.callAOHandler(std::move(aoHandler));
    };
}

}   // namespace nhope
