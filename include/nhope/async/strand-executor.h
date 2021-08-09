#pragma once

#include <memory>
#include <nhope/async/executor.h>

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
     * @remark Задачи, отправленные через StrandExecutor, но которые еще не были выполнены, 
     *       все равно будут выполнены последовательно.
     *       
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
