#include <utility>
#include <boost/asio/steady_timer.hpp>
#include "nhope/asyncs/timer.h"

namespace {
using namespace nhope::asyncs;

class AsioSteadyTimerImpl final : public Timer
{
public:
    AsioSteadyTimerImpl(boost::asio::io_context& ctx, const std::chrono::nanoseconds& expiryTime, Handler&& handler)
      : m_timer(ctx)
    {
        m_isCancelled = std::make_shared<bool>(false);
        m_timer.expires_after(expiryTime);

        m_timer.async_wait([this, isCancelled = m_isCancelled, handler = std::move(handler)](auto& code) {
            if (*isCancelled == false) {
                handler(code, *this);
            }
        });
    }

    ~AsioSteadyTimerImpl() override
    {
        *m_isCancelled = true;
        m_timer.cancel();
    }

    bool isExpired() const override
    {
        const auto expiryTime = m_timer.expiry();
        return expiryTime >= std::chrono::steady_clock::now();
    }

private:
    boost::asio::steady_timer m_timer;
    std::shared_ptr<bool> m_isCancelled;
};

}   // namespace

std::unique_ptr<Timer> Timer::start(boost::asio::io_context& ctx, const std::chrono::nanoseconds& expiryTime,
                                    Handler&& handler)
{
    return std::make_unique<AsioSteadyTimerImpl>(ctx, expiryTime, std::move(handler));
}
