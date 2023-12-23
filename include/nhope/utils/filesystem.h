#pragma once

#include <filesystem>
#include <string_view>

namespace nhope {

std::filesystem::path makeTemporaryDirectory(std::string_view prefix);

}   // namespace nhope
