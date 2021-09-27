#pragma once

#include <cstdint>
#include <memory>
#include <string_view>
#include <vector>

#include "nhope/async/ao-context.h"
#include "nhope/io/io-device.h"
#include "nhope/async/thread-pool-executor.h"
#include "nhope/async/future.h"

namespace nhope {

enum class OpenFileMode
{
    ReadOnly,
    WriteOnly,
};

class FileDevice : public IODevice
{};
using FileDevicePtr = std::unique_ptr<FileDevice>;

FileDevicePtr openFile(AOContext& aoCtx, std::string_view fileName, OpenFileMode mode);
Future<std::vector<std::uint8_t>> readFile(AOContext& aoCtx, std::string_view fileName);

}   // namespace nhope
