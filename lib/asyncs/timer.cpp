#include <memory>
#include <utility>

#include <boost/asio/steady_timer.hpp>

#include "nhope/asyncs/ao-context.h"
#include "nhope/asyncs/thread-executor.h"
#include "nhope/asyncs/timer.h"

void nhope::asyncs::setTimeout(AOContext& aoCtx, std::chrono::nanoseconds timeout,
                               std::function<void(const std::error_code&)>&& handler)
{
    auto& executor = aoCtx.executor();
    auto& ioCtx = executor.getContext();

    auto timer = std::make_shared<boost::asio::steady_timer>(ioCtx);
    auto cancel = [timer] {
        timer->cancel();
    };

    timer->expires_after(timeout);
    timer->async_wait(aoCtx.newAsyncOperation(std::move(handler), std::move(cancel)));
}
