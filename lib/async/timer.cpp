#include <atomic>
#include <cassert>
#include <chrono>
#include <exception>
#include <functional>
#include <memory>
#include <system_error>
#include <utility>

#include <asio/io_context.hpp>
#include <asio/steady_timer.hpp>

#include "nhope/async/ao-context-close-handler.h"
#include "nhope/async/ao-context-error.h"
#include "nhope/async/ao-context.h"
#include "nhope/async/executor.h"
#include "nhope/async/future.h"
#include "nhope/async/timer.h"

namespace nhope {
namespace {

using SteadyClock = std::chrono::steady_clock;
using TimePoint = std::chrono::steady_clock::time_point;

class SingleTimer final
  : public std::enable_shared_from_this<SingleTimer>
  , public AOContextCloseHandler
{
public:
    SingleTimer(AOContext& aoCtx, std::function<void(const std::error_code&)> handler)
      : m_aoCtxRef(aoCtx)
      , m_impl(aoCtx.executor().ioCtx())
      , m_handler(std::move(handler))
    {
        m_aoCtxRef.addCloseHandler(*this);
    }

    ~SingleTimer()
    {
        m_aoCtxRef.removeCloseHandler(*this);
    }

    void start(std::chrono::nanoseconds timeout)
    {
        m_impl.expires_at(SteadyClock::now() + timeout);
        m_impl.async_wait([self = shared_from_this()](auto err) {
            if (err == std::errc::operation_canceled) {
                return;
            }

            self->m_aoCtxRef.exec(
              [self, err] {
                  self->m_handler(err);
              },
              Executor::ExecMode::ImmediatelyIfPossible);
        });
    }

    void aoContextClose() noexcept override
    {
        m_impl.cancel();
    }

private:
    AOContextRef m_aoCtxRef;
    asio::steady_timer m_impl;
    std::function<void(const std::error_code&)> m_handler;
};

class PromiseTimer final
  : public std::enable_shared_from_this<PromiseTimer>
  , public AOContextCloseHandler
{
public:
    explicit PromiseTimer(AOContext& aoCtx)
      : m_aoCtxRef(aoCtx)
      , m_impl(aoCtx.executor().ioCtx())
    {
        m_aoCtxRef.addCloseHandler(*this);
    }

    ~PromiseTimer()
    {
        m_aoCtxRef.removeCloseHandler(*this);
    }

    Future<void> start(std::chrono::nanoseconds timeout)
    {
        m_impl.expires_at(SteadyClock::now() + timeout);
        m_impl.async_wait([self = shared_from_this()](auto err) mutable {
            bool expectedFlag = false;
            if (!self->m_promiseResolved.compare_exchange_strong(expectedFlag, true, std::memory_order_relaxed,
                                                                 std::memory_order_relaxed)) {
                return;
            }

            if (err) {
                self->m_promise.setException(std::make_exception_ptr(std::system_error(err)));
                return;
            }

            self->m_promise.setValue();
        });

        return m_promise.future();
    }

    void aoContextClose() noexcept override
    {
        bool expectedFlag = false;
        if (!m_promiseResolved.compare_exchange_strong(expectedFlag, true, std::memory_order_relaxed,
                                                       std::memory_order_relaxed)) {
            return;
        }

        m_impl.cancel();
        m_promise.setException(std::make_exception_ptr(AsyncOperationWasCancelled()));
    }

private:
    AOContextRef m_aoCtxRef;
    asio::steady_timer m_impl;
    Promise<void> m_promise;
    std::atomic<bool> m_promiseResolved = false;
};

class IntervalTimer final
  : public std::enable_shared_from_this<IntervalTimer>
  , public AOContextCloseHandler
{
public:
    IntervalTimer(AOContext& aoCtx, std::chrono::nanoseconds interval,
                  std::function<bool(const std::error_code&)> handler)
      : m_aoCtxRef(aoCtx)
      , m_impl(aoCtx.executor().ioCtx())
      , m_interval(interval)
      , m_handler(std::move(handler))
    {
        m_aoCtxRef.addCloseHandler(*this);
    }

    ~IntervalTimer()
    {
        m_aoCtxRef.removeCloseHandler(*this);
    }

    void start()
    {
        m_tickTime = SteadyClock::now();
        this->startNextTick();
    }

    void aoContextClose() noexcept override
    {
        m_impl.cancel();
    }

private:
    void startNextTick()
    {
        m_tickTime += m_interval;
        m_impl.expires_at(m_tickTime);
        m_impl.async_wait([self = shared_from_this()](auto err) {
            if (err == std::errc::operation_canceled) {
                return;
            }

            self->m_aoCtxRef.exec(
              [self, err] {
                  try {
                      if (!self->m_handler(err)) {
                          // The timer needs to be stopped
                          return;
                      }

                      if (err) {
                          // The timer was broken
                          return;
                      }

                      self->startNextTick();
                  } catch (...) {
                  }
              },
              Executor::ExecMode::ImmediatelyIfPossible);
        });
    }

    AOContextRef m_aoCtxRef;
    asio::steady_timer m_impl;
    const std::chrono::nanoseconds m_interval;
    const std::function<bool(const std::error_code& err)> m_handler;
    TimePoint m_tickTime;
};

}   // namespace

void setTimeout(AOContext& aoCtx, std::chrono::nanoseconds timeout, std::function<void(const std::error_code&)> handler)
{
    assert(handler != nullptr);     // NOLINT
    assert(timeout.count() >= 0);   // NOLINT

    auto timer = std::make_shared<SingleTimer>(aoCtx, std::move(handler));
    timer->start(timeout);
}

Future<void> setTimeout(AOContext& aoCtx, std::chrono::nanoseconds timeout)
{
    assert(timeout.count() >= 0);   // NOLINT

    auto timer = std::make_shared<PromiseTimer>(aoCtx);
    return timer->start(timeout);
}

void setInterval(AOContext& aoCtx, std::chrono::nanoseconds interval,
                 std::function<bool(const std::error_code&)> handler)
{
    assert(handler != nullptr);     // NOLINT
    assert(interval.count() > 0);   // NOLINT

    auto timer = std::make_shared<IntervalTimer>(aoCtx, interval, std::move(handler));
    timer->start();
}

}   // namespace nhope
