#pragma once

#include <memory>

#include "nhope/async/ao-context-error.h"
#include "nhope/async/detail/ao-context-impl.h"
#include "nhope/async/executor.h"

namespace nhope {

/**
 * @brief Контекст для выполнения асинхронных операций на заданном Executor
 * 
 * AOContext решает следующие задачи:
 * - обеспечивает вызов обработчика асинхронной операции (AOHandler::call) в заданном Executor-e
 * - гарантирует, что все обработчики асинхронных операций, запущенные на одном AOContext-е,
 *   будут выполнены последовательно
 * - гарантирует, что при уничтожении контекста все асинхронные операции, запущенные
 *   на контексте, будут отменены (AOHandler::cancel) а их обработчики вызваны не будут
 *   (AOHandler::call)
 *
 * @see AOHandler
 * @see AOHandlerCall
 */
class AOContext final
{
    friend class AOContextRef;

public:
    AOContext(const AOContext&) = delete;
    AOContext& operator=(const AOContext&) = delete;

    AOContext(AOContext&&) noexcept = delete;
    AOContext& operator=(AOContext&&) = delete;

    /**
     * @brief Конструктор AOContext
     * 
     * @param executor executor, на котром AOContext должен выполнять свои операции.
     *
     * @note executor должен существовать, пока AOContext открыт.
     *
     * @see AOContext::executor
     */
    explicit AOContext(Executor& executor);

    /**
     * @brief Конструктор для создания дочернего AOContext
     *
     * Свойства дочернего AOContext
     * 1. Дочерний AOContext использует executor родительского AOContext (AOContext::executor).
     *    Это гарантирует, что обработчики асинхронных операций родителя и дочерних AOContext
     *    будут выполняться строго последовательно. 
     * 2. При закрытии родительского AOContext все дочерние AOContext будут закрыты автоматически.
     * 
     * @param parent Родительский AOContext.
     *
     * @pre parent.isOpen() == true
     *
     * @see AOContext::close
     * @see AOContext::executor
     */
    explicit AOContext(AOContext& parent);

    /**
     * @brief Деструктор AOContext
     *
     * Производит закрытие AOContext и отмену незавершенных операций
     *
     * @see AOHandler
     * @see AOContext::close()
     */
    ~AOContext();

    [[nodiscard]] bool isOpen() const noexcept;

    /**
     * @brief Производит закрытие AOContext и отмену незавершенных операций
     *
     * Если close вызовут одновременно несколько потоков, то фактическое закрытие будет
     * осуществлять только один из потоков, остальны потоки просто дождутся окончание закрытия.
     *
     * @note Метод потокобезопасный
     *
     * @see AOHandler
     */
    void close();

    /**
     * @brief Возвращает executor AOContext-а
     *
     * @note Обратите внимание, что это не тот же executor, что передавался в при создании AOContext.
     *       Поверх переданного executor AOContext создает StrandExecutor, чтобы гарантировать последовательное
     *       выполнение операций внутри AOContext. Executor AOContext будет уничтожен в момент уничтожения AOContext.
     * 
     * @note Метод потокобезопасный
     */
    SequenceExecutor& executor();

    template<typename Work>
    void exec(Work&& work, Executor::ExecMode mode = Executor::ExecMode::AddInQueue)
    {
        m_aoImpl->exec(std::forward<Work>(work), mode);
    }

    void addCloseHandler(AOContextCloseHandler& closeHandler);
    void removeCloseHandler(AOContextCloseHandler& closeHandler) noexcept;

    /**
     * @brief Проверяет работает ли AOContext в потоке, из которого произведен
     *        вызов workInThisThread.
     *
     * @note метод потокобезопасный
     *
     * @return true AOContext работает в этом потоке.
     * @return false AOContext в данный момент не работает в этом потоке.
     */
    [[nodiscard]] bool workInThisThread() const noexcept;

private:
    detail::RefPtr<detail::AOContextImpl> m_aoImpl;
};

/**
 * @brief Ссылка на AOContext
 * 
 * На AOContext может быть несколько ссылок. Ссылки не могут закрыть или блокировать
 * закрытие AOContext.
 *
 * Ссылки нужны для тех случаев, когда нам нужен доступ к AOContext, но мы не можем
 * контролировать время жизни контекста.
 *
 * @see AOContext
 */
class AOContextRef final
{
public:
    explicit AOContextRef(AOContext& aoCtx) noexcept;

    [[nodiscard]] bool isOpen() const noexcept;

    template<typename Work>
    void exec(Work&& work, Executor::ExecMode mode = Executor::ExecMode::AddInQueue)
    {
        m_aoImpl->exec(std::forward<Work>(work), mode);
    }

    void addCloseHandler(AOContextCloseHandler& closeHandler);
    void removeCloseHandler(AOContextCloseHandler& closeHandler) noexcept;

    [[nodiscard]] bool workInThisThread() const noexcept;

private:
    detail::RefPtr<detail::AOContextImpl> m_aoImpl;
};

}   // namespace nhope
