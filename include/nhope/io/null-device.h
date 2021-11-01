#pragma once

#include <memory>
#include <vector>

#include "nhope/io/io-device.h"

namespace nhope {

class NullDevice;
using NullDevicePtr = std::unique_ptr<NullDevice>;

class NullDevice : public IODevice
{
public:
    static NullDevicePtr create(AOContext& aoCtx);
};

}   // namespace nhope
