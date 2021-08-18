#include <cstddef>
#include <list>
#include <memory>
#include <thread>

#include <asio/dispatch.hpp>
#include <asio/io_context.hpp>
#include <asio/post.hpp>

#include "nhope/async/thread-pool-executor.h"

namespace nhope {

using WorkGuard = asio::executor_work_guard<asio::io_context::executor_type>;

struct ThreadPoolExecutor::Impl final
{
    explicit Impl(std::size_t threadCount)
      : ioCtx(static_cast<int>(threadCount))
      , workGuard(ioCtx.get_executor())
    {
        try {
            for (std::size_t i = 0; i < threadCount; ++i) {
                threads.emplace_back([this] {
                    ioCtx.run();
                });
            }
        } catch (...) {
            this->stop();
            throw;
        }
    }

    ~Impl()
    {
        this->stop();
    }

    void stop()
    {
        ioCtx.stop();
        for (auto& thread : threads) {
            thread.join();
        }
    }

    asio::io_context ioCtx;
    std::list<std::thread> threads;
    WorkGuard workGuard;
};

ThreadPoolExecutor::ThreadPoolExecutor(std::size_t threadCount)
  : m_d(std::make_unique<Impl>(threadCount))
{}

ThreadPoolExecutor::~ThreadPoolExecutor() = default;

std::size_t ThreadPoolExecutor::threadCount() const noexcept
{
    return m_d->threads.size();
}

void ThreadPoolExecutor::exec(Work work, ExecMode mode)
{
    if (mode == ExecMode::AddInQueue) {
        asio::post(m_d->ioCtx, std::move(work));
    } else {
        asio::dispatch(m_d->ioCtx, std::move(work));
    }
}

asio::io_context& ThreadPoolExecutor::ioCtx()
{
    return m_d->ioCtx;
}

ThreadPoolExecutor& ThreadPoolExecutor::defaultExecutor()
{
    static auto executor = ThreadPoolExecutor(std::thread::hardware_concurrency());
    return executor;
}

}   // namespace nhope
