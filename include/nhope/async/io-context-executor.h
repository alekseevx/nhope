#pragma once

#include "nhope/async/executor.h"

namespace nhope {

/**
 * @brief Позволяет обернуть внешний io_context в Executor
 */
class IOContextExecutor final : public Executor
{
public:
    explicit IOContextExecutor(asio::io_context& ioCtx);

    void exec(Work work, ExecMode mode = ExecMode::AddInQueue) override;
    asio::io_context& ioCtx() override;

private:
    asio::io_context& m_ioCtx;
};

/**
 * @brief Позволяет обернуть внешний io_context в SequenceExecutor
 * @remark Предполагается, что заданный io_context будет вызван 
 *         только на одном потоке.
 */
class IOContextSequenceExecutor final : public SequenceExecutor
{
public:
    explicit IOContextSequenceExecutor(asio::io_context& ioCtx);

    void exec(Work work, ExecMode mode = ExecMode::AddInQueue) override;
    asio::io_context& ioCtx() override;

private:
    asio::io_context& m_ioCtx;
};

}   // namespace nhope
