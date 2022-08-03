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
      , m_aoCtxRef(aoCtx)
    {
        m_aoCtxRef.startCancellableTask(
          [&] {
              m_impl.expires_at(SteadyClock::now() + timeout);
              m_impl.async_wait([this, aoCtxRef = m_aoCtxRef](auto err) mutable {
                  aoCtxRef.exec(
                    [this, err] {
                        this->wakeup(err);
                    },
                    Executor::ExecMode::ImmediatelyIfPossible);
              });
          },
          *this);
    }

    ~SingleTimer() override
    {
        m_aoCtxRef.removeCloseHandler(*this);
    }

private:
    void aoContextClose() noexcept override
    {
        // AOContext закрыт, таймер больше не нужен.
        m_impl.cancel();

        // Можно спокойно удалять себя - AOContext проследит, чтобы wakeup не был вызван.
        delete this;
    }

    void wakeup(std::error_code err)
    {
        auto handler = std::move(m_handler);
        delete this;   // Таймер сработал и больше не нужен

        handler(err);
    }

    asio::steady_timer m_impl;
    std::function<void(const std::error_code&)> m_handler;

    AOContextRef m_aoCtxRef;
};

class PromiseTimer final : public AOContextCloseHandler
{
public:
    explicit PromiseTimer(AOContext& aoCtx, Promise<void>&& promise, std::chrono::nanoseconds timeout)
      : m_impl(aoCtx.executor().ioCtx())
      , m_promise(std::move(promise))
      , m_aoCtxRef(aoCtx)
    {
        m_aoCtxRef.startCancellableTask(
          [&] {
              m_impl.expires_at(SteadyClock::now() + timeout);
              m_impl.async_wait([this, aoCtxRef = m_aoCtxRef](auto err) mutable {
                  aoCtxRef.exec(
                    [this, err] {
                        this->wakeup(err);
                    },
                    Executor::ExecMode::ImmediatelyIfPossible);
              });
          },
          *this);
    }

    ~PromiseTimer() override
    {
        m_aoCtxRef.removeCloseHandler(*this);
    }

private:
    void wakeup(std::error_code err)
    {
        auto promise = std::move(m_promise);
        delete this;   // Таймер сработал и больше не нужен

        if (err) {
            promise.setException(std::make_exception_ptr(std::system_error(err)));
            return;
        }
        promise.setValue();
    }

    void aoContextClose() noexcept override
    {
        // AOContext закрыт, таймер больше не нужен.
        m_impl.cancel();

        m_promise.setException(std::make_exception_ptr(AsyncOperationWasCancelled()));

        // Можно спокойно удалять себя - AOContext проследит, чтобы wakeup не был вызван.
        delete this;
    }

    asio::steady_timer m_impl;
    Promise<void> m_promise;
    AOContextRef m_aoCtxRef;
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
        m_aoCtx.startCancellableTask(
          [&] {
              m_tickTime = SteadyClock::now();
              this->startNextTick(AOContextRef(m_aoCtx));
          },
          *this);
    }

    ~IntervalTimer() override
    {
        m_aoCtx.removeCloseHandler(*this);
    }

private:
    void aoContextClose() noexcept override
    {
        // AOContext закрыт, таймер больше не нужен.
        m_impl.cancel();

        // Можно спокойно удалять себя - AOContext проследит, чтобы wakeup не был вызван.
        delete this;
    }

    void startNextTick(AOContextRef&& aoCtxRef)
    {
        m_tickTime += m_interval;
        m_impl.expires_at(m_tickTime);
        m_impl.async_wait([this, aoCtxRef = std::move(aoCtxRef)](auto err) mutable {
            aoCtxRef.exec(
              [this, err] {
                  this->wakeup(err);
              },
              Executor::ExecMode::ImmediatelyIfPossible);
        });
    }

    void wakeup(std::error_code err)
    {
        // FIXME: https://gitlab.olimp.lan/alekseev/nhope/-/issues/25
        // Защищаемся от закрытия AOContext в handler-е.
        // Закрытие AOContext приведет к немедленному вызову aoContextClose
        // и, соответственно, к уничтожению m_aoCtx, m_handler.
        // m_aoCtx нам нужен, чтобы понять, нужно ли начинать новый цикл.
        // m_handler нельзя уничтожать, пока он не завершит работу.
        auto aoCtxRef = AOContextRef(m_aoCtx);
        auto handler = std::move(m_handler);

        try {
            const bool continueFlag = handler(err);
            if (!aoCtxRef.isOpen()) {
                // AOContext уже закрыт. Это значит, что таймер был уничтожен
                // в aoContextClose и к его полям обращаться нельзя.
                return;
            }

            if (!continueFlag) {
                this->stopped();
                return;
            }

            if (err) {
                this->stopped();
                return;
            }
        } catch (...) {
        }

        m_handler = std::move(handler);
        this->startNextTick(std::move(aoCtxRef));
    }

    void stopped()
    {
        m_aoCtx.close();
    }

    asio::steady_timer m_impl;
    const std::chrono::nanoseconds m_interval;
    std::function<bool(const std::error_code& err)> m_handler;
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
