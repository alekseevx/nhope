#pragma once

#include <memory>

#include "nhope/async/ao-context-error.h"
#include "nhope/async/detail/ao-context-impl.h"
#include "nhope/async/executor.h"

namespace nhope {

class AOContextRef;

/**
 * @brief Контекст для последовательного выполнения задач на заданном Executor
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
     * @param executor executor на котором AOContext должен выполнять свои операции.
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
     *    Это гарантирует, что задачи родителя и дочерних AOContext будут выполняться строго последовательно. 
     * 2. При закрытии родительского AOContext все дочерние AOContext будут закрыты автоматически.
     * 
     * @param parent Родительский AOContext.
     *
     * @pre parent.isOpen() == true
     *
     * @see AOContext::close
     * @see AOContext::executor
     *
     * @throw AOContextClosed Если родительский AOContext уже закрыт
     */
    explicit AOContext(AOContext& parent);
    explicit AOContext(AOContextRef& parent);

    /**
     * @brief Деструктор AOContext
     *
     * Производит закрытие AOContext
     *
     * @see AOHandler
     * @see AOContext::close
     * @note Метод потокобезопасный
     */
    ~AOContext();

    [[nodiscard]] bool isOpen() const noexcept;

    /**
     * @brief Производит закрытие AOContext
     *
     * Если close вызовут одновременно несколько потоков, то фактическое закрытие будет
     * осуществлять только один из потоков, остальны потоки просто дождутся окончания закрытия.
     *
     * Последовательность действий при закрытии:
     * - Переходим в состояние "Подготовка к закрытию". Теперь isOpen() == false.
     *   AOContext задачи больше не запускает.
     * - Если есть активная задача и close делается не из нее - дожидаемся завершения.
     * - Вызываем зарегистрированные обработчики закрытия AOContext (#addCloseHandler).
     * - AOContext полностью закрыт.
     *
     * @note Задачи, ожидающие выполнения, будут отброшены.
     * @note Метод потокобезопасный
     * @post isOpen() == false
     */
    void close();

    /**
     * @brief Возвращает executor AOContext-а
     *
     * @note Обратите внимание, что это не тот же executor, что передавался при создании AOContext.
     *       Поверх переданного executor AOContext создает StrandExecutor чтобы гарантировать последовательное
     *       выполнение операций внутри AOContext. Executor AOContext будет уничтожен в момент уничтожения AOContext.
     * 
     * @note Метод потокобезопасный
     */
    SequenceExecutor& executor();

    /**
     * @brief Планирует новую задачу для выполнения в рамках AOContext.
     * 
     * Задачи выполняются строго последовательно на executor-е AOContext.
     *
     * @param work планируемая задача
     *
     * @see close
     * @note метод потокобезопасный
     */
    template<typename Work>
    void exec(Work&& work, Executor::ExecMode mode = Executor::ExecMode::AddInQueue)
    {
        m_aoImpl->exec(std::forward<Work>(work), mode);
    }

    /**
     * @brief Добавляет обработчик закрытия AOContext
     *
     * @note Метод потокобезопасный
     * @pre isOpen() == true
     * @throw AOContextClosed Если AOContext уже закрыт
     * @note Перед удалением обработчика необходимо вызвать removeCloseHandler 
     */
    void addCloseHandler(AOContextCloseHandler& closeHandler);

    /**
     * @brief Удаляет обработчик закрытия AOContext
     * @note Метод потокобезопасный
     */
    void removeCloseHandler(AOContextCloseHandler& closeHandler) noexcept;

    /**
     * @brief Проверяет работает ли AOContext в потоке из которого произведен
     *        вызов workInThisThread (workInThisThread вызван из выполняемой задачи).
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
    friend class AOContext;

public:
    AOContextRef(AOContext& aoCtx) noexcept;

    [[nodiscard]] bool isOpen() const noexcept;

    SequenceExecutor& executor();

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
