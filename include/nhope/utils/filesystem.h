#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>

namespace nhope {

std::filesystem::path makeTemporaryDirectory(std::string_view prefix);

}   // namespace nhope
