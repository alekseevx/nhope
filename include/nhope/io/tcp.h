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

class TcpSocket;
using TcpSocketPtr = std::unique_ptr<TcpSocket>;

class TcpSocket : public IODevice
{
public:
    enum class Shutdown
    {
        Receive,   // Shutdown the receive side of the socket.
        Send,      // Shutdown the send side of the socket
        Both       // Shutdown both send and receive on the socket.
    };

    virtual void shutdown(Shutdown = Shutdown::Both) = 0;

    static Future<TcpSocketPtr> connect(AOContext& aoCtx, std::string_view hostName, std::uint16_t port);
};

class TcpServer;
using TcpServerPtr = std::unique_ptr<TcpServer>;

class TcpServer
{
public:
    virtual ~TcpServer() = default;

    virtual Future<TcpSocketPtr> accept() = 0;

    static TcpServerPtr start(AOContext& aoCtx, const TcpServerParams& params);
};

}   // namespace nhope
