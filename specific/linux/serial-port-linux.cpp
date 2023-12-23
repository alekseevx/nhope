#include <filesystem>
#include <list>
#include <regex>

#include "nhope/io/serial-port.h"

namespace nhope {

namespace {
using namespace std::literals;
bool checkAlive(std::string_view deviceName)
{
    namespace fs = std::filesystem;
    const std::string sysFsPath = "/sys/class/tty/"s + deviceName.data();
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

std::list<std::string> SerialPort::availableDevices()
{
    std::list<std::string> devices;

    namespace fs = std::filesystem;

    static const std::string prefix{"/dev/"};
    static const std::regex re("tty(S\\d+)|(USB)|(XRUSB)|(ACM)|(AMA)|(AP)|(comm)");

    for (const auto& entry : fs::directory_iterator(prefix)) {
        std::cmatch m;
        const std::string entryName = entry.path().filename().c_str();
        if (std::regex_search(entryName.c_str(), m, re)) {
            std::string comName = m[0];
            if (checkAlive(comName)) {
                devices.emplace_back(prefix + comName);
            }
        }
    }

    return devices;
}

}   // namespace nhope
