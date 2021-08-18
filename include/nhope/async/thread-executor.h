#pragma once

#include <memory>
#include <string>
#include <thread>

#include "nhope/async/executor.h"

namespace nhope {

class ThreadExecutor final : public SequenceExecutor
{
public:
    using Id = std::thread::id;

    explicit ThreadExecutor(const std::string& name = "ThrEx");
    ~ThreadExecutor() override;

    [[nodiscard]] Id id() const noexcept;

    void exec(Work work, ExecMode mode = ExecMode::AddInQueue) override;
    asio::io_context& ioCtx() override;

private:
    struct Impl;
    std::unique_ptr<Impl> m_d;
};

}   // namespace nhope
