#pragma once

#include <chrono>
#include <functional>
#include <system_error>

#include "nhope/async/future.h"

namespace nhope {

void setTimeout(AOContext& aoCtx, std::chrono::nanoseconds timeout,
                std::function<void(const std::error_code&)>&& handler);

Future<void> setTimeout(AOContext& aoCtx, std::chrono::nanoseconds timeout);

}   // namespace nhope
