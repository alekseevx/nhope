#include <filesystem>
#include <random>
#include <system_error>
#include <utility>

#include "fmt/core.h"
#include "nhope/utils/filesystem.h"

namespace nhope {

std::filesystem::path makeTemporaryDirectory(std::string_view prefix)
{
    auto tmpDir = std::filesystem::temp_directory_path();
    std::random_device dev;
    std::mt19937 prng(dev());
    std::uniform_int_distribution<uint16_t> rand(0);
    std::filesystem::path path;
    while (true) {
        path = tmpDir / fmt::format("{0}{1:X}", prefix, rand(prng));
        // true if the directory was created.
        if (std::filesystem::create_directory(path)) {
            break;
        }
    }
    return path;
}

}   // namespace nhope
