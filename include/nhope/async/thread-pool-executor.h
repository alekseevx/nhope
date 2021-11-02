#pragma once

#include <cstddef>
#include <memory>
#include <list>
#include <string>
#include <thread>

#include "nhope/async/executor.h"
#include "nhope/utils/detail/fast-pimpl.h"

namespace nhope {

class ThreadPoolExecutor final : public Executor
{
public:
    explicit ThreadPoolExecutor(std::size_t threadCount, const std::string& name = "ThrPoolEx");
    ~ThreadPoolExecutor() override;

    [[nodiscard]] std::size_t threadCount() const noexcept;

    void exec(Work work, ExecMode mode = ExecMode::AddInQueue) override;
    asio::io_context& ioCtx() override;

    static ThreadPoolExecutor& defaultExecutor();

private:
    struct Impl;
    static constexpr std::size_t implSize{72};
    nhope::detail::FastPimpl<Impl, implSize> m_d;
};

}   // namespace nhope
