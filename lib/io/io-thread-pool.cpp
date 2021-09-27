#include "io-thread-pool.h"

namespace nhope::detail {

ThreadPoolExecutor& ioThreadPool()
{
    constexpr auto threadCount = 2;
    static ThreadPoolExecutor pool(threadCount, "io");
    return pool;
}

}   // namespace nhope::detail
