#include <cstddef>
#include <cstring>

#include <linux/prctl.h>
#include <sys/prctl.h>

#include "nhope/async/detail/thread-name.h"

namespace nhope::detail {

constexpr std::size_t maxTheardNameLen = 16;

void setThreadName(const std::string& name)
{
    // NOLINTNEXTLINE
    char buf[maxTheardNameLen] = {};

    // NOLINTNEXTLINE
    strncpy(buf, name.c_str(), maxTheardNameLen);

    // NOLINTNEXTLINE
    prctl(PR_SET_NAME, buf, 0L, 0L, 0L);
}

}   // namespace nhope::detail
