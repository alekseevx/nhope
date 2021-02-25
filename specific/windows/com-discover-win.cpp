#include <cstdlib>
#include <vector>
#include <string>

#include "nhope/utils/com-discover.h"

#include <Windows.h>

namespace nhope::utils {

std::vector<std::string> getAvailableComs()
{
    std::vector<std::string> result;
    HKEY hKey = nullptr;

    if (::RegOpenKeyExA(HKEY_LOCAL_MACHINE, "HARDWARE\\DEVICEMAP\\SERIALCOMM", 0, KEY_QUERY_VALUE, &hKey) !=
        ERROR_SUCCESS) {
        return result;
    }

    DWORD index = 0;

    // This is a maximum length of value name, see:

    // https://msdn.microsoft.com/en-us/library/windows/desktop/ms724872%28v=vs.85%29.aspx
    constexpr auto maximumValueNameInChars = 16383;

    std::vector<char> outputValueName(maximumValueNameInChars, 0);
    std::vector<char> outputBuffer(MAX_PATH + 1, 0);

    DWORD bytesRequired = MAX_PATH;

    while (true) {
        DWORD requiredValueNameChars = maximumValueNameInChars;

        const LONG ret = ::RegEnumValueA(hKey, index, &outputValueName[0], &requiredValueNameChars, nullptr, nullptr,
                                         reinterpret_cast<LPBYTE>(&outputBuffer[0]), &bytesRequired);   // NOLINT

        if (ret != ERROR_SUCCESS) {
            break;
        }
        result.emplace_back(outputBuffer.data());
        ++index;
    }
    ::RegCloseKey(hKey);
    return result;
}

}   // namespace nhope::utils