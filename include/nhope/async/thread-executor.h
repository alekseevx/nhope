#pragma once

#include <string>
#include <thread>

#include "nhope/async/executor.h"
#include "nhope/utils/detail/fast-pimpl.h"

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
    static constexpr std::size_t implSize{72};
    nhope::detail::FastPimpl<Impl, implSize> m_d;
};

}   // namespace nhope
