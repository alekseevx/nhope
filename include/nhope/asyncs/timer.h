#pragma once

#include <chrono>
#include <functional>
#include <system_error>

namespace nhope::asyncs {

class AOContext;

void setTimeout(AOContext& aoCtx, std::chrono::nanoseconds timeout,
                std::function<void(const std::error_code&)>&& handler);

}   // namespace nhope::asyncs
