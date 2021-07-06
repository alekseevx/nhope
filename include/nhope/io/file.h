#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "nhope/io/io-device.h"
#include "nhope/async/thread-pool-executor.h"
#include "nhope/async/future.h"

namespace nhope {

enum class FileMode
{
    ReadOnly,
    WriteOnly,
    ReadWrite
};

struct FileSettings
{
    std::string fileName;
    FileMode mode;
};

std::unique_ptr<IoDevice> openFile(nhope::Executor& executor, const FileSettings& settings);
Future<std::vector<std::uint8_t>> readFile(const std::filesystem::path& fileName,
                                           Executor& executor = ThreadPoolExecutor::defaultExecutor());

}   // namespace nhope