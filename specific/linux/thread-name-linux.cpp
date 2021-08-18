
#include <linux/prctl.h>
#include <sys/prctl.h>
#include "nhope/async/detail/thread-name.h"

namespace nhope::detail {

void setThreadName(const std::string& name)
{
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg)
    prctl(PR_SET_NAME, name.c_str(), 0L, 0L, 0L);
}

}   // namespace nhope::detail
