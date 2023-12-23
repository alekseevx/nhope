#pragma once

#include <cstdint>
#include <memory>
#include <optional>

#include "nhope/async/ao-context.h"
#include "nhope/io/io-device.h"
#include "nhope/io/sock-addr.h"

namespace nhope {

class UdpSocket;
using UdpSocketPtr = std::unique_ptr<UdpSocket>;

class UdpSocket
  : public IODevice
  , public IOCancellable
{
public:
    struct Endpoint
    {
        std::string address;
        std::uint16_t port;
    };

    struct Params
    {
        Endpoint bindAddress;
        std::optional<Endpoint> peerAddress;
        bool nonBlocking = true;

        std::optional<bool> broadcast;
        std::optional<bool> reuseAddress;
        std::optional<int> receiveBufferSize;
        std::optional<int> sendBufferSize;
    };

    using NativeHandle = uintptr_t;

    [[nodiscard]] virtual NativeHandle nativeHandle() = 0;
    [[nodiscard]] virtual SockAddr localAddress() const = 0;
    [[nodiscard]] virtual SockAddr peerAddress() const = 0;

    static UdpSocketPtr create(AOContext& aoCtx, const Params& params);
    // wraps already prepared socket
    static UdpSocketPtr create(AOContext& aoCtx, NativeHandle native);
};

}   // namespace nhope
