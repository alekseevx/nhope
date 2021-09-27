#pragma once

#include "nhope/async/thread-pool-executor.h"

namespace nhope::detail {

ThreadPoolExecutor& ioThreadPool();

}   // namespace nhope::detail
