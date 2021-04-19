#pragma once

#include <functional>
#include <nhope/utils/noncopyable.h>

namespace asio {
class io_context;
}

namespace nhope {

/**
 * @class Executor
 *
 * @brief Интерфейс executor-а
 */
class Executor : Noncopyable
{
public:
    using Work = std::function<void()>;

    virtual ~Executor() = default;

    /**
     * Добавляет функцию в очередь для выполнения на заданном executor-е.
     *
     * @note В зависимости от реализации задачи могут выполнять как параллельно,
     * так и последовательно.
     */
    virtual void post(Work work) = 0;

    /**
     * Функция для получения контекста для выполнения операций ввода-вывода на заданном
     * executor-е.
     * Если executor не поддерживает операции ввода-вывода, будет сгенерировано исключение.
     */
    virtual asio::io_context& ioCtx() = 0;
};

/**
 * @class SequenceExecutor
 *
 * @brief Базовый класс для executor-ов, гарантирующих последовательное выполнение задач.
 */
class SequenceExecutor : public Executor
{
public:
    ~SequenceExecutor() override = default;

    /**
     * Добавляет функцию в очередь для выполнения на заданном executor-е.
     * Гарантируется, что задачи будут выполняться последовательно.
     */
    void post(Work work) override = 0;
};

}   // namespace nhope
