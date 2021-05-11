#include "asio/io_context.hpp"
#include <chrono>
#include <cstdint>
#include <exception>
#include <functional>
#include <memory>
#include <stdexcept>
#include <system_error>
#include <utility>

#include <asio/steady_timer.hpp>

#include <nhope/async/ao-context.h>
#include <nhope/async/future.h>
#include <nhope/async/thread-executor.h>
#include <nhope/async/timer.h>

namespace nhope {

void setTimeout(AOContext& aoCtx, std::chrono::nanoseconds timeout, std::function<void(const std::error_code&)> handler)
{
    auto& executor = aoCtx.executor();
    auto& ioCtx = executor.ioCtx();

    auto timer = std::make_shared<asio::steady_timer>(ioCtx);
    auto cancel = [timer] {
        timer->cancel();
    };

    timer->expires_after(timeout);
    timer->async_wait(aoCtx.newAsyncOperation(std::move(handler), std::move(cancel)));
}

Future<void> setTimeout(AOContext& aoCtx, std::chrono::nanoseconds timeout)
{
    auto& executor = aoCtx.executor();
    auto& ioCtx = executor.ioCtx();

    auto promise = std::make_shared<Promise<void>>();
    auto future = promise->future();

    auto timer = std::make_shared<asio::steady_timer>(ioCtx);
    std::function handler = [promise](const std::error_code& err) {
        if (err) {
            auto exPtr = std::make_exception_ptr(std::system_error(err));
            promise->setException(exPtr);
        }

        promise->setValue();
    };
    std::function cancel = [timer, promise] {
        auto exPtr = std::make_exception_ptr(AsyncOperationWasCancelled());
        promise->setException(exPtr);
        timer->cancel();
    };

    timer->expires_after(timeout);
    timer->async_wait(aoCtx.newAsyncOperation(std::move(handler), std::move(cancel)));

    return future;
}

namespace {

using time_point = std::chrono::steady_clock::time_point;

class IntervalTimer final : public std::enable_shared_from_this<IntervalTimer>
{
public:
    explicit IntervalTimer(AOContext& aoCtx, std::chrono::nanoseconds interval,
                           std::function<bool(const std::error_code&)> handler)
      : m_handler(std::move(handler))
      , m_interval(interval)
      , m_timerImpl(aoCtx.executor().ioCtx())
    {
        // m_safeOnTick ensures that onTick will only be called if the aoCtx exists
        m_safeOnTick = aoCtx.makeSafeCallback(std::function([this](const std::error_code& err) {
            this->onTick(err);
        }));
    }

    void start()
    {
        m_startTime = std::chrono::steady_clock::now();

        /* We block the removal of us to a stop  */
        m_anchor = shared_from_this();

        this->startNextTick();
    }

private:
    void startNextTick()
    {
        const auto nextTickTime = m_startTime + (++m_tickNum * m_interval);

        m_timerImpl.expires_at(nextTickTime);
        m_timerImpl.async_wait([this](const auto& err) {
            try {
                this->m_safeOnTick(err);
            } catch (const AOContextClosed&) {
                // The AOContext was destroyed, the timer stopped
                this->stop();
            } catch (...) {
                // There was an error, the timer stopped
                this->stop();
            }
        });
    }

    void onTick(const std::error_code& err)
    {
        if (!m_handler(err)) {
            this->stop();
            return;
        }

        if (err) {
            // Timer was broken
            this->stop();
            return;
        }

        this->startNextTick();
    }

    void stop()
    {
        m_anchor.reset();
    }

    std::function<bool(const std::error_code&)> m_handler;
    std::function<void(const std::error_code&)> m_safeOnTick;

    std::chrono::nanoseconds m_interval;

    std::uint64_t m_tickNum = 0;
    time_point m_startTime{};
    asio::steady_timer m_timerImpl;

    std::shared_ptr<IntervalTimer> m_anchor;
};

}   // namespace

void setInterval(AOContext& aoCtx, std::chrono::nanoseconds interval,
                 std::function<bool(const std::error_code&)> handler)
{
    const auto timer = std::make_shared<IntervalTimer>(aoCtx, interval, std::move(handler));
    timer->start();
}

}   // namespace nhope
