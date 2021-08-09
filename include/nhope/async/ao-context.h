#pragma once

#include <memory>

#include "nhope/async/ao-handler.h"
#include "nhope/async/ao-context-error.h"
#include "nhope/async/executor.h"

namespace nhope {

namespace detail {
class AOContextImpl;
}

/**
 * @brief Класс для вызова AOHandler в контексте AOContext.
 */
class AOHandlerCall final
{
    friend class AOContext;
    friend class AOContextWeekRef;

public:
    AOHandlerCall() = default;

    /**
     * @brief Вызов AOHandler-а в контексте AOContext-а.
     * @note Вызов можно произвести только один раз.
     */
    void operator()(Executor::ExecMode mode = Executor::ExecMode::AddInQueue);

private:
    using AOContextImplPtr = std::shared_ptr<detail::AOContextImpl>;

    AOHandlerCall(AOHandlerId id, AOContextImplPtr aoImpl);

    AOHandlerId m_id{};
    AOContextImplPtr m_aoImpl;
};

/**
 * @class AOContext
 *
 * @brief Контекст для выполнения асинхронных операций на заданном Executor
 * 
 * AOContext решает следующие задачи:
 * - обеспечивает вызов обработчика асинхронной операции (#AOHandler::call) в заданном Executor-e
 * - гарантирует, что все обработчики асинхронных операций, запущенные на одном AOContext-е,
 *   будут выполнены последовательно
 * - гарантирует, что при уничтожении контекста все асинхронные операции, запущенные
 *   на контексте, будут отменены (#AOHandler::cancel) а их обработчики вызваны не будут
 *   (#AOHandler::call)
 */
class AOContext final
{
    friend class AOContextWeekRef;

public:
    AOContext(const AOContext&) = delete;
    AOContext& operator=(const AOContext&) = delete;

    AOContext(AOContext&&) noexcept = default;
    AOContext& operator=(AOContext&&) = delete;

    explicit AOContext(Executor& executor);
    ~AOContext();

    SequenceExecutor& executor();

    /**
     * Помещает AOHandler в AOContext.
     */
    [[nodiscard]] AOHandlerCall putAOHandler(std::unique_ptr<AOHandler> handler);
    void callAOHandler(std::unique_ptr<AOHandler> handler, Executor::ExecMode mode = Executor::ExecMode::AddInQueue);

    /**
     * @brief Проверяет работает ли AOContext в потоке, из которого произведен
     *        вызов workInThisThread.
     * 
     * @return true AOContext работает в этом потоке.
     * @return false AOContext в данный момент не работает в этом потоке.
     */
    [[nodiscard]] bool workInThisThread() const;

private:
    using AOContextImplPtr = std::shared_ptr<detail::AOContextImpl>;
    AOContextImplPtr m_aoImpl;
};

class AOContextWeekRef final
{
public:
    explicit AOContextWeekRef(AOContext& aoCtx);

    [[nodiscard]] AOHandlerCall putAOHandler(std::unique_ptr<AOHandler> handler);
    void callAOHandler(std::unique_ptr<AOHandler> handler, Executor::ExecMode mode = Executor::ExecMode::AddInQueue);

private:
    using AOContextImplPtr = std::shared_ptr<detail::AOContextImpl>;
    AOContextImplPtr m_aoImpl;
};

}   // namespace nhope
