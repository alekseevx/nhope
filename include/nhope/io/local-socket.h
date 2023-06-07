#pragma once

#include <memory>

#include "nhope/async/ao-context.h"
#include "nhope/io/io-device.h"

namespace nhope {

struct LocalServerParams
{
    std::string address;
};

class LocalSocket
  : public IODevice
  , public IOCancellable
{
public:
    static Future<std::unique_ptr<LocalSocket>> connect(AOContext& aoCtx, std::string_view address);
};

class LocalServer
{
public:
    virtual ~LocalServer() = default;

    virtual Future<std::unique_ptr<LocalSocket>> accept() = 0;

    static std::unique_ptr<LocalServer> start(AOContext& aoCtx, const LocalServerParams& params);
};

}   // namespace nhope
