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

class File;
using FilePtr = std::unique_ptr<File>;

class File : public IODevice
{
public:
    static FilePtr open(AOContext& aoCtx, std::string_view fileName, OpenFileMode mode);
    static Future<std::vector<std::uint8_t>> readAll(AOContext& aoCtx, std::string_view fileName);
};

}   // namespace nhope
