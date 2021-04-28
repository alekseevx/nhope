#include <nhope/async/thread-pool-executor.h>

namespace nhope {

ThreadPoolExecutor::ThreadPoolExecutor(std::size_t threadCount)
  : m_ioCtx(static_cast<int>(threadCount))
{
    try {
        for (std::size_t i = 0; i < threadCount; ++i) {
            m_threads.emplace_back(std::thread([this] {
                auto workGuard = asio::make_work_guard(m_ioCtx);
                m_ioCtx.run();
            }));
        }
    } catch (...) {
        this->stop();
        throw;
    }
}

ThreadPoolExecutor::~ThreadPoolExecutor()
{
    this->stop();
}

std::size_t ThreadPoolExecutor::threadCount() const noexcept
{
    return m_threads.size();
}

void ThreadPoolExecutor::post(Work work)
{
    m_ioCtx.post(std::move(work));
}

asio::io_context& ThreadPoolExecutor::ioCtx()
{
    return m_ioCtx;
}

ThreadPoolExecutor& ThreadPoolExecutor::defaultExecutor()
{
    static auto executor = ThreadPoolExecutor(std::thread::hardware_concurrency());
    return executor;
}

void ThreadPoolExecutor::stop()
{
    m_ioCtx.stop();
    for (auto& thread : m_threads) {
        thread.join();
    }
    m_threads.clear();
}

}   // namespace nhope
