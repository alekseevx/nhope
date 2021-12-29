#include <cassert>
#include <chrono>
#include <exception>
#include <functional>
#include <system_error>
#include <utility>

#include <asio/io_context.hpp>
#include <asio/steady_timer.hpp>

#include "nhope/async/ao-context-close-handler.h"
#include "nhope/async/ao-context-error.h"
#include "nhope/async/ao-context.h"
#include "nhope/async/future.h"
#include "nhope/async/timer.h"
#include "nhope/utils/scope-exit.h"

namespace nhope {
namespace {

using SteadyClock = std::chrono::steady_clock;
using TimePoint = std::chrono::steady_clock::time_point;

class SingleTimer final : public AOContextCloseHandler
{
public:
    SingleTimer(AOContext& aoCtx, std::chrono::nanoseconds timeout, std::function<void(const std::error_code&)> handler)
      : m_impl(aoCtx.executor().ioCtx())
      , m_handler(std::move(handler))
      , m_aoCtx(aoCtx)
    {
        this->start(timeout);
        m_aoCtx.addCloseHandler(*this);
    }

    ~SingleTimer()
    {
        m_aoCtx.removeCloseHandler(*this);
    }

private:
    void start(std::chrono::nanoseconds timeout)
    {
        m_impl.expires_at(SteadyClock::now() + timeout);
        m_impl.async_wait([this, aoCtxRef = AOContextRef(m_aoCtx)](auto err) mutable {
            aoCtxRef.exec(
              [this, err] {
                  this->wakeup(err);
              },
              Executor::ExecMode::ImmediatelyIfPossible);
        });
    }

    void aoContextClose() noexcept override
    {
        // AOContext закрыт, таймер больше не нужен.
        m_impl.cancel();

        // Можно спокойно удалять себя - AOContext проследит, чтобы wakeup не был вызван.
        delete this;
    }

    void wakeup(std::error_code err)
    {
        ScopeExit clear([this] {
            m_aoCtx.close();
        });

        m_handler(err);
    }

    asio::steady_timer m_impl;
    std::function<void(const std::error_code&)> m_handler;

    AOContext m_aoCtx;
};

class PromiseTimer final : public AOContextCloseHandler
{
public:
    explicit PromiseTimer(AOContext& aoCtx, Promise<void>&& promise, std::chrono::nanoseconds timeout)
      : m_impl(aoCtx.executor().ioCtx())
      , m_promise(std::move(promise))
      , m_aoCtx(aoCtx)
    {
        this->start(timeout);
        m_aoCtx.addCloseHandler(*this);
    }

    ~PromiseTimer()
    {
        m_aoCtx.removeCloseHandler(*this);
    }

private:
    void start(std::chrono::nanoseconds timeout)
    {
        m_impl.expires_at(SteadyClock::now() + timeout);
        m_impl.async_wait([this, aoCtxRef = AOContextRef(m_aoCtx)](auto err) mutable {
            aoCtxRef.exec(
              [this, err] {
                  this->wakeup(err);
              },
              Executor::ExecMode::ImmediatelyIfPossible);
        });
    }

    void wakeup(std::error_code err)
    {
        ScopeExit clear([this] {
            // Теперь можно спокойно удалять себя.
            m_aoCtx.close();
        });

        if (err) {
            m_promise.setException(std::make_exception_ptr(std::system_error(err)));
            return;
        }

        m_promise.setValue();
    }

    void aoContextClose() noexcept override
    {
        // AOContext закрыт, таймер больше не нужен.
        m_impl.cancel();

        if (!m_promise.satisfied()) {
            m_promise.setException(std::make_exception_ptr(AsyncOperationWasCancelled()));
        }

        // Можно спокойно удалять себя - AOContext проследит, чтобы wakeup не был вызван.
        delete this;
    }

    asio::steady_timer m_impl;
    Promise<void> m_promise;
    AOContext m_aoCtx;
};

class IntervalTimer final : public AOContextCloseHandler
{
public:
    IntervalTimer(AOContext& aoCtx, std::chrono::nanoseconds interval,
                  std::function<bool(const std::error_code&)> handler)
      : m_impl(aoCtx.executor().ioCtx())
      , m_interval(interval)
      , m_handler(std::move(handler))
      , m_aoCtx(aoCtx)
    {
        this->start();
        m_aoCtx.addCloseHandler(*this);
    }

    ~IntervalTimer()
    {
        m_aoCtx.removeCloseHandler(*this);
    }

private:
    void start()
    {
        m_tickTime = SteadyClock::now();
        this->startNextTick();
    }

    void aoContextClose() noexcept override
    {
        // AOContext закрыт, таймер больше не нужен.
        m_impl.cancel();

        // Можно спокойно удалять себя - AOContext проследит, чтобы wakeup не был вызван.
        delete this;
    }

    void startNextTick()
    {
        m_tickTime += m_interval;
        m_impl.expires_at(m_tickTime);
        m_impl.async_wait([this, aoCtxRef = AOContextRef(m_aoCtx)](auto err) mutable {
            aoCtxRef.exec(
              [this, err] {
                  this->wakeup(err);
              },
              Executor::ExecMode::ImmediatelyIfPossible);
        });
    }

    void wakeup(std::error_code err)
    {
        try {
            if (!m_handler(err)) {
                // The timer needs to be stopped
                this->stopped();
                return;
            }

            if (err) {
                // The timer was broken
                this->stopped();
                return;
            }
        } catch (...) {
        }

        this->startNextTick();
    }

    void stopped()
    {
        m_aoCtx.close();
    }

    asio::steady_timer m_impl;
    const std::chrono::nanoseconds m_interval;
    const std::function<bool(const std::error_code& err)> m_handler;
    TimePoint m_tickTime;

    AOContext m_aoCtx;
};

}   // namespace

void setTimeout(AOContext& aoCtx, std::chrono::nanoseconds timeout, std::function<void(const std::error_code&)> handler)
{
    assert(handler != nullptr);     // NOLINT
    assert(timeout.count() >= 0);   // NOLINT

    new SingleTimer(aoCtx, timeout, std::move(handler));
}

Future<void> setTimeout(AOContext& aoCtx, std::chrono::nanoseconds timeout)
{
    assert(timeout.count() >= 0);   // NOLINT

    auto [future, promise] = makePromise();
    new PromiseTimer(aoCtx, std::move(promise), timeout);
    return std::move(future);
}

void setInterval(AOContext& aoCtx, std::chrono::nanoseconds interval,
                 std::function<bool(const std::error_code&)> handler)
{
    assert(handler != nullptr);     // NOLINT
    assert(interval.count() > 0);   // NOLINT

    new IntervalTimer(aoCtx, interval, std::move(handler));
}

}   // namespace nhope
