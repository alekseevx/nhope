#include <array>
#include <cstddef>
#include <cstring>

#include <linux/prctl.h>
#include <sys/prctl.h>

#include "nhope/async/detail/thread-name.h"

namespace nhope::detail {

void setThreadName(const std::string& name)
{
    constexpr std::size_t maxThreadNameLen = 15;
    std::array<char, maxThreadNameLen + 1> buf{};

    std::strncpy(buf.data(), name.c_str(), buf.size() - 1);

    // NOLINTNEXTLINE
    prctl(PR_SET_NAME, buf.data(), 0L, 0L, 0L);
}

}   // namespace nhope::detail
