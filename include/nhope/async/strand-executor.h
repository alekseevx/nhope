#pragma once

#include <memory>
#include "nhope/async/executor.h"

namespace nhope {

/**
 * @class StrandExecutor
 *
 * Обеспечивает последовательное выполнение задач на заданном
 * Executor-е.
 */
class StrandExecutor final : public SequenceExecutor
{
public:
    explicit StrandExecutor(Executor& executor);

    /**
     * @remark Задачи, находящиеся в очереди, будут отброшены.
     * @remark Деструктор не дожидается окончания текущей активной задачи.
     */
    ~StrandExecutor() override;

    Executor& originExecutor() noexcept;

    void exec(Work work, ExecMode mode = ExecMode::AddInQueue) override;
    asio::io_context& ioCtx() override;

private:
    class Impl;
    std::shared_ptr<Impl> m_d;
};

}   // namespace nhope
