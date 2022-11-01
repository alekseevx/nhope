#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "nhope/async/ao-context.h"
#include "nhope/io/io-device.h"
#include "nhope/io/sock-addr.h"

namespace nhope {

struct TcpServerParams
{
    std::string address;
    std::uint16_t port;
};

class TcpSocket;
using TcpSocketPtr = std::unique_ptr<TcpSocket>;

class TcpSocket
  : public IODevice
  , public IOCancellable
{
public:
    using NativeHandle = uintptr_t;

    enum class Shutdown
    {
        Receive,   // Shutdown the receive side of the socket.
        Send,      // Shutdown the send side of the socket
        Both       // Shutdown both send and receive on the socket.
    };

    struct Options
    {
        std::optional<bool> keepAlive;
        std::optional<bool> reuseAddress;
        std::optional<int> receiveBufferSize;
        std::optional<int> sendBufferSize;
    };

    virtual void setOptions(const Options& opts) = 0;
    [[nodiscard]] virtual Options options() const = 0;

    [[nodiscard]] virtual NativeHandle nativeHandle() = 0;

    [[nodiscard]] virtual SockAddr localAddress() const = 0;
    [[nodiscard]] virtual SockAddr peerAddress() const = 0;
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
    [[nodiscard]] virtual SockAddr bindAddress() const = 0;

    static TcpServerPtr start(AOContext& aoCtx, const TcpServerParams& params);
};

}   // namespace nhope
