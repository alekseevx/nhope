#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <system_error>

#include "nhope/async/detail/future.h"
#include "nhope/async/executor.h"
#include "nhope/io/io-device.h"
#include "nhope/io/network.h"

namespace nhope {

class TcpError final : public IoError
{
public:
    explicit TcpError(std::error_code errCode);
};

struct TcpClientParam
{
    Endpoint endpoint;
};

struct TcpServerParam
{
    Endpoint endpoint;
};

class TcpServer
{
public:
    virtual Future<IoDevicePtr> accept() = 0;
    virtual ~TcpServer() = default;
};

Future<IoDevicePtr> connect(Executor& e, const TcpClientParam& settings);
std::unique_ptr<TcpServer> listen(Executor& e, const TcpServerParam& settings);

}   // namespace nhope