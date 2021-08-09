#include <cstddef>
#include <list>
#include <thread>

#include <asio/io_context.hpp>

#include <nhope/async/executor.h>

namespace nhope {

class ThreadPoolExecutor final : public Executor
{
public:
    explicit ThreadPoolExecutor(std::size_t threadCount);
    ~ThreadPoolExecutor() override;

    [[nodiscard]] std::size_t threadCount() const noexcept;

    void exec(Work work, ExecMode mode = ExecMode::AddInQueue) override;
    asio::io_context& ioCtx() override;

    static ThreadPoolExecutor& defaultExecutor();

private:
    void stop();

    asio::io_context m_ioCtx;
    std::list<std::thread> m_threads;
};

}   // namespace nhope