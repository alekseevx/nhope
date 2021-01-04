#include <exception>
#include <functional>
#include <memory>
#include <system_error>
#include <utility>

#include <boost/asio/steady_timer.hpp>

#include "nhope/async/ao-context.h"
#include "nhope/async/future.h"
#include "nhope/async/thread-executor.h"
#include "nhope/async/timer.h"

using namespace nhope;

void nhope::setTimeout(AOContext& aoCtx, std::chrono::nanoseconds timeout,
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

Future<void> nhope::setTimeout(AOContext& aoCtx, std::chrono::nanoseconds timeout)
{
    auto promise = std::make_shared<Promise<void>>();
    auto future = promise->future();

    auto& executor = aoCtx.executor();
    auto& ioCtx = executor.getContext();

    auto timer = std::make_shared<boost::asio::steady_timer>(ioCtx);
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
