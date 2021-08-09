#pragma once

#include <thread>
#include <utility>

#include <asio/io_context.hpp>

#include <nhope/async/executor.h>
#include <nhope/utils/noncopyable.h>

namespace nhope {

class ThreadExecutor final : public SequenceExecutor
{
public:
    using Id = std::thread::id;

    ThreadExecutor();
    ~ThreadExecutor() override;

    [[nodiscard]] Id id() const noexcept;

    void exec(Work work, ExecMode mode = ExecMode::AddInQueue) override;
    asio::io_context& ioCtx() override;

private:
    asio::io_context m_ioCtx;
    std::thread m_thread;
};

}   // namespace nhope
