#include <cassert>
#include <chrono>
#include <exception>
#include <functional>
#include <memory>
#include <system_error>
#include <utility>

#include <asio/io_context.hpp>
#include <asio/steady_timer.hpp>

#include "nhope/async/ao-context.h"
#include "nhope/async/future.h"
#include "nhope/async/thread-executor.h"
#include "nhope/async/timer.h"

namespace nhope {
namespace {

using SteadyClock = std::chrono::steady_clock;
using TimePoint = std::chrono::steady_clock::time_point;

class Timer final : public std::enable_shared_from_this<Timer>
{
public:
    explicit Timer(asio::io_context& ioCtx)
      : m_impl(ioCtx)
    {}

    void start(TimePoint expiresAt, AOHandlerCall callAOHandler)
    {
        m_callAOHandler = std::move(callAOHandler);
        m_impl.expires_at(expiresAt);
        m_impl.async_wait([weekSelf = weak_from_this()](const std::error_code err) {
            if (err == std::errc::operation_canceled) {
                return;
            }

            const auto self = weekSelf.lock();
            if (self == nullptr) {
                return;
            }

            self->m_errorCode = err;
            self->m_callAOHandler(Executor::ExecMode::ImmediatelyIfPossible);
        });
    }

    void cancel()
    {
        m_impl.cancel();
    }

    [[nodiscard]] const std::error_code& errorCode() const
    {
        return m_errorCode;
    }

private:
    AOHandlerCall m_callAOHandler;
    asio::steady_timer m_impl;
    std::error_code m_errorCode;
};

class SingleTimerAOHandler final : public AOHandler
{
public:
    using UserHandler = std::function<void(const std::error_code&)>;

    SingleTimerAOHandler(std::shared_ptr<Timer> timer, UserHandler userHandler)
      : m_timer(std::move(timer))
      , m_userHandler(std::move(userHandler))
    {}

    void call() override
    {
        m_userHandler(m_timer->errorCode());
    }

    void cancel() override
    {
        m_timer->cancel();
    }

private:
    const std::shared_ptr<Timer> m_timer;
    const UserHandler m_userHandler;
};

class FutureTimerAOHandler final : public AOHandler
{
public:
    FutureTimerAOHandler(std::shared_ptr<Timer> timer, Promise<void> promise)
      : m_timer(std::move(timer))
      , m_promise(std::move(promise))
    {}

    void call() override
    {
        const auto& err = m_timer->errorCode();
        if (err) {
            auto exPtr = std::make_exception_ptr(std::system_error(err));
            m_promise.setException(std::move(exPtr));
            return;
        }

        m_promise.setValue();
    }

    void cancel() override
    {
        auto exPtr = std::make_exception_ptr(AsyncOperationWasCancelled());
        m_promise.setException(std::move(exPtr));
        m_timer->cancel();
    }

private:
    const std::shared_ptr<Timer> m_timer;
    Promise<void> m_promise;
};

struct IntervalTimerData final
{
    IntervalTimerData(AOContext& aoCtx, std::shared_ptr<Timer> timer, std::chrono::nanoseconds interval,
                      std::function<bool(const std::error_code&)> userHandler)
      : aoCtx(aoCtx)
      , timer(std::move(timer))
      , interval(interval)
      , userHandler(std::move(userHandler))
      , tickTime(SteadyClock::now())
    {}

    TimePoint nextTickTime()
    {
        tickTime += interval;
        return tickTime;
    }

    AOContextRef aoCtx;

    const std::shared_ptr<Timer> timer;
    const std::chrono::nanoseconds interval;
    const std::function<bool(const std::error_code&)> userHandler;

    TimePoint tickTime;
};

class IntervalTimerAOHandler final : public AOHandler
{
public:
    explicit IntervalTimerAOHandler(std::unique_ptr<IntervalTimerData> d)
      : m_d(std::move(d))
    {}

    void call() override
    {
        assert(m_d != nullptr);   // NOLINT

        try {
            const auto& err = m_d->timer->errorCode();

            if (!m_d->userHandler(err)) {
                // The timer needs to be stopped
                return;
            }

            if (err) {
                // The timer was broken
                return;
            }

            this->startNextTick();
        } catch (...) {
        }
    }

    void cancel() override
    {
        m_d->timer->cancel();
    }

private:
    void startNextTick()
    {
        auto& timer = *m_d->timer;
        auto& aoCtx = m_d->aoCtx;

        const auto nextTickTimet = m_d->nextTickTime();
        auto aoHandler = std::make_unique<IntervalTimerAOHandler>(std::move(m_d));

        timer.start(nextTickTimet, aoCtx.putAOHandler(std::move(aoHandler)));
    }

    std::unique_ptr<IntervalTimerData> m_d;
};

}   // namespace

void setTimeout(AOContext& aoCtx, std::chrono::nanoseconds timeout, std::function<void(const std::error_code&)> handler)
{
    assert(handler != nullptr);     // NOLINT
    assert(timeout.count() >= 0);   // NOLINT

    auto& executor = aoCtx.executor();
    auto& ioCtx = executor.ioCtx();

    auto timer = std::make_shared<Timer>(ioCtx);
    auto aoHandler = std::make_unique<SingleTimerAOHandler>(timer, std::move(handler));
    auto callAOHandler = aoCtx.putAOHandler(std::move(aoHandler));

    timer->start(SteadyClock::now() + timeout, std::move(callAOHandler));
}

Future<void> setTimeout(AOContext& aoCtx, std::chrono::nanoseconds timeout)
{
    assert(timeout.count() >= 0);   // NOLINT

    auto promise = Promise<void>();
    auto future = promise.future();
    auto& executor = aoCtx.executor();
    auto& ioCtx = executor.ioCtx();

    auto timer = std::make_shared<Timer>(ioCtx);
    auto aoHandler = std::make_unique<FutureTimerAOHandler>(timer, std::move(promise));
    auto callAOHandler = aoCtx.putAOHandler(std::move(aoHandler));

    timer->start(SteadyClock::now() + timeout, std::move(callAOHandler));

    return future;
}

void setInterval(AOContext& aoCtx, std::chrono::nanoseconds interval,
                 std::function<bool(const std::error_code&)> handler)
{
    assert(handler != nullptr);     // NOLINT
    assert(interval.count() > 0);   // NOLINT

    auto& executor = aoCtx.executor();
    auto& ioCtx = executor.ioCtx();

    auto timer = std::make_shared<Timer>(ioCtx);
    auto intervalTimerData = std::make_unique<IntervalTimerData>(aoCtx, timer, interval, std::move(handler));
    auto firstTickTime = intervalTimerData->nextTickTime();
    auto aoHandler = std::make_unique<IntervalTimerAOHandler>(std::move(intervalTimerData));
    auto callAOHandler = aoCtx.putAOHandler(std::move(aoHandler));

    timer->start(firstTickTime, std::move(callAOHandler));
}

}   // namespace nhope
