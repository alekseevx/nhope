#pragma once

#include <functional>
#include <memory>
#include <utility>

#include "nhope/async/ao-context-error.h"
#include "nhope/async/ao-context.h"
#include "nhope/async/executor.h"

namespace nhope {

enum AOContextClosedActions
{
    ThrowAOContextClosed,
    NotThrowAOContextClosed
};

/**
 * @brief Функция для создания безопасного callback-а
 *
 * Безопасный callback обладает следующими свойствами:
 * - Безопасный callback можно вызывать из любого потока, исходный callback
 * будет асинхронно вызван в executor-е AOContext-а.
 * - Безопасный callback может быть вызван после уничтожения AOContext-а. В этом
 * случае реакция определяется параметром aoContextClosedActions.
 *
 * @retval Безопасный callback
 */
template<typename... Args>
std::function<void(Args...)> makeSafeCallback(AOContext& aoCtx, std::function<void(Args...)> callback,
                                              AOContextClosedActions aoContextClosedActions = ThrowAOContextClosed)
{
    // https://gitlab.olimp.lan/alekseev/nhope/-/issues/8
    auto callbackPtr = std::make_shared<std::function<void(Args...)>>(std::move(callback));

    return [aoContextClosedActions, aoCtx = AOContextRef(aoCtx), callbackPtr](Args... args) mutable {
        if (aoContextClosedActions == ThrowAOContextClosed && !aoCtx.isOpen()) {
            throw AOContextClosed();
        };

        aoCtx.exec([callbackPtr, args...] {
            (*callbackPtr)(args...);
        });
    };
}

template<typename Callback>
auto makeSafeCallback(AOContext& aoCtx, Callback&& callback,
                      AOContextClosedActions aoContextClosedActions = ThrowAOContextClosed)
{
    return makeSafeCallback(aoCtx, std::function(std::forward<Callback>(callback)), aoContextClosedActions);
}

}   // namespace nhope
