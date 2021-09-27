#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

#include "nhope/async/ao-context.h"
#include "nhope/io/io-device.h"

namespace nhope {

struct TcpServerParams
{
    std::string address;
    std::uint16_t port;
};

class TcpSocket : public IODevice
{
public:
};
using TcpSocketPtr = std::unique_ptr<TcpSocket>;

class TcpServer
{
public:
    virtual ~TcpServer() = default;

    virtual Future<TcpSocketPtr> accept() = 0;
};
using TcpServerPtr = std::unique_ptr<TcpServer>;

Future<TcpSocketPtr> connect(AOContext& aoCtx, std::string_view hostName, std::uint16_t port);

TcpServerPtr listen(AOContext& aoCtx, const TcpServerParams& params);

}   // namespace nhope
