#pragma once

#include <climits>
#include <cstdint>
#include <memory>

#include "nhope/async/detail/ao-handler-id.h"
#include "nhope/async/executor.h"

namespace nhope {

namespace detail {
class AOContextImpl;
}

/**
 * @brief Обработчик завершения асинхронной операции.
 *
 * Используется, когда требуется, чтобы обработчик был вызван в 
 * в рамках AOContext, но выполнение самой асинхронная операции
 * может проходить вне AOContext.
 *
 * @see AOContext
 * @see AOHandlerCall
 *
 * @code 
 * class MyAOHandler : public nhope::AOHandler {
 * public:
 *   void call() override
 *   {
 *      // Здесь код, который должен быть вызван в 
 *      // в executor-е AOContext по завершению операции.
 *      // Если же AOContext будет закрыт во время выполнения операции,
 *      // этот код вызывать нельзя.
 *   }
 *
 *   void cancel() override
 *   {
 *      // Код для отмены асинхронной операции
 *   }
 * };
 *
 * void startAsyncOperation(AOContext& aoCtx)
 * {
 *   auto aoHandler = std::make_unique<MyAOHandler>();
 *
 *   // Отдадим aoHandler aoCtx-у, aoCtx же вернёт нам объект для вызова нашего обработчика.
 *   // callAOHandler мы запоминаем у себя, aoCtx нам больше не нужен.
 *   // Как только наша операция завершится, мы при помощи callAOHandler безопасно попытаемся вызвать наш
 *   // обработчик в executor aoCtx. Если aoCtx жив, вызов будет произвден асинхронно,
 *   // если нет - ничего не произойдет.
 *   auto callAOHandler = aoCtx.putAOHandler(std::move(aoHandler));
 *   std::thread([callAOHandler]{
 *      // Код асинхронной операции
 *      // ...
 *      // Операция завершена, вызываем наш обработчик.
 *      callAOHandler(); 
 *   }).detach()
 * };
 * @endcode
 */
class AOHandler
{
public:
    virtual ~AOHandler() = default;

    /**
     * @brief Вызов обработчика завершения асинхронной операции.
     *
     * @remark Вызывается из потока AOContext.
     * @remark Гарантируется, что будет вызвано либо call, либо cancel.
     * @remark AOHandler можно вызвать только один раз.
     */
    virtual void call() = 0;

    /**
     * @brief Вызывается при отмене асинхронной операции.
     *
     * Если операция еще не завершилась (AOHandler::call не вызывался) а AOContext
     * закрывается, в этом случае AOContext произведет вызов cancel, чтобы 
     * асинхронная операция могла произвести действия по своей отмене.
     *
     * @remark Вызывается из потока, в котором производится закрытие AOContext.
     * @remark Гарантируется, что будет вызвано либо AOHandler::call, либо AOHandler::cancel.
     */
    virtual void cancel() = 0;
};

/**
 * @brief Реализует вызов AOHandler в контексте AOContext.
 */
class AOHandlerCall final
{
    friend class AOContext;
    friend class AOContextRef;

public:
    AOHandlerCall() = default;

    /**
     * @brief Вызов AOHandler-а в контексте AOContext-а.
     *
     * @note Вызов можно произвести только один раз.
     *
     * @see AOContext
     * @see AOHandler
     */
    void operator()(Executor::ExecMode mode = Executor::ExecMode::AddInQueue);

private:
    using AOContextImplPtr = std::shared_ptr<detail::AOContextImpl>;
    using AOHandlerId = detail::AOHandlerId;

    AOHandlerCall(AOHandlerId id, AOContextImplPtr aoImpl);

    AOHandlerId m_id = detail::invalidAOHandlerId;
    AOContextImplPtr m_aoImpl;
};

}   // namespace nhope
