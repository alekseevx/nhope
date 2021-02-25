#include <filesystem>
#include <regex>
#include <set>

#include "nhope/utils/com-discover.h"

namespace nhope::utils {

namespace {
using namespace std::literals;
bool checkAlive(std::string_view comName)
{
    namespace fs = std::filesystem;
    const std::string sysFsPath = "/sys/class/tty/"s + comName.data();
    const std::string devicePath = sysFsPath + "/device"s;
    const std::string driverPath = devicePath + "/driver"s;
    bool isAlive = fs::exists(driverPath) && fs::exists(devicePath);
    if (isAlive) {
        constexpr auto platform = "platform"sv;
        const auto absoluteDevPath = fs::absolute(devicePath).append("subsystem");
        const auto realPath = fs::read_symlink(absoluteDevPath);
        const std::string subsystem = realPath.filename();
        isAlive &= subsystem != platform;
    }

    return isAlive;
}

}   // namespace

std::vector<std::string> getAvailableComs()
{
    std::vector<std::string> res;

    namespace fs = std::filesystem;

    static const std::string prefix{"/dev/"};
    static const std::regex re("tty(S\\d+)|(USB)|(XRUSB)|(ACM)|(AMA)|(AP)|(comm)");

    for (const auto& entry : fs::directory_iterator(prefix)) {
        std::string s;
        std::cmatch m;
        if (std::regex_search(entry.path().filename().c_str(), m, re)) {
            std::string comName = m[0];
            if (checkAlive(comName)) {
                res.emplace_back(prefix + comName);
            }
        }
    }

    return res;
}

}   // namespace nhope::utils