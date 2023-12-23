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
    enum class ExecMode
    {
        AddInQueue,              // Гарантирует, что Work не будет запущен из exec
        ImmediatelyIfPossible,   // Запустить Work прямо в exec-e, если это возможно
    };

    virtual ~Executor() = default;

    /**
     * Добавляет функцию в очередь для выполнения на заданном executor-е.
     *
     * @note В зависимости от реализации задачи могут выполнять как параллельно,
     * так и последовательно.
     */
    virtual void exec(Work work, ExecMode mode = ExecMode::AddInQueue) = 0;

    /**
     * Функция для получения контекста для выполнения операций ввода-вывода на заданном
     * executor-е.
     * Если executor не поддерживает операции ввода-вывода, будет сгенерировано исключение.
     */
    virtual asio::io_context& ioCtx() = 0;

    [[deprecated("Use exec instead")]] void post(Work work)
    {
        this->exec(std::move(work), ExecMode::AddInQueue);
    }
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
    void exec(Work work, ExecMode mode = ExecMode::AddInQueue) override = 0;
};

}   // namespace nhope
