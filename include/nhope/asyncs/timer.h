#pragma once

#include <chrono>
#include <functional>
#include <system_error>

#include "future.h"

namespace nhope {

class AOContext;

void setTimeout(AOContext& aoCtx, std::chrono::nanoseconds timeout,
                std::function<void(const std::error_code&)>&& handler);

Future<void> setTimeout(AOContext& aoCtx, std::chrono::nanoseconds timeout);

}   // namespace nhope
