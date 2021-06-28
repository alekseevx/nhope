#pragma once

#include <string>

#include "nhope/io/io-device.h"

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

}   // namespace nhope