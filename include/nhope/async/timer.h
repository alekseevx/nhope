#pragma once

#include <chrono>
#include <functional>
#include <system_error>

#include <nhope/async/future.h>

namespace nhope {

void setTimeout(AOContext& aoCtx, std::chrono::nanoseconds timeout,
                std::function<void(const std::error_code&)> handler);

Future<void> setTimeout(AOContext& aoCtx, std::chrono::nanoseconds timeout);

/**
 * Starts a periodic timer, which is triggered at a specified time interval.
 * 
 * The timer will stop in one of the following cases:
 * - Destroying AOContext;
 * - handler returned false;
 * - timer was broken (check error_code)
 */
void setInterval(AOContext& aoCtx, std::chrono::nanoseconds interval,
                 std::function<bool(const std::error_code& err)> handler);

}   // namespace nhope
