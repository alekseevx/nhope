#include <Windows.h>
#include "nhope/async/detail/thread-name.h"

namespace nhope::detail {

namespace {
/// See <http://msdn.microsoft.com/en-us/library/xcb2z8hs.aspx>
/// and <http://blogs.msdn.com/b/stevejs/archive/2005/12/19/505815.aspx> for
/// more information on the code below.

const DWORD MS_VC_EXCEPTION = 0x406D1388;

#pragma pack(push, 8)
struct ThreadNameInfo
{
    DWORD dwType;       // Must be 0x1000.
    LPCSTR szName;      // Pointer to name (in user addr space).
    DWORD dwThreadID;   // Thread ID (-1=caller thread).
    DWORD dwFlags;      // Reserved for future use, must be zero.
};
#pragma pack(pop)

}   // namespace

void setThreadName(const std::string& name)
{
    ThreadNameInfo info{};
    info.dwType = 0x1000;   // NOLINT(readability-magic-numbers)
    info.szName = name.c_str();
    info.dwThreadID = GetCurrentThreadId();
    info.dwFlags = 0;

    __try {
        // NOLINTNEXTLINE
        RaiseException(MS_VC_EXCEPTION, 0, sizeof(info) / sizeof(ULONG_PTR), reinterpret_cast<ULONG_PTR*>(&info));
    } __except (EXCEPTION_CONTINUE_EXECUTION) {
    }
}

}   // namespace nhope::detail
