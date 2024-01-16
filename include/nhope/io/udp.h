#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

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

        bool operator==(const Endpoint& rhs) const noexcept
        {
            return port == rhs.port && address == rhs.address;
        }
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

/**
 * @brief UdpMultiPeerSocket on write send data to peer list
 */
class UdpMultiPeerSocket : virtual public UdpSocket
{
public:
    // peer list for resending
    [[nodiscard]] virtual std::vector<Endpoint> peers() const noexcept = 0;
    // add peer for resending
    virtual void addPeer(Endpoint ep) = 0;
    // remove peer from resending
    virtual void removePeer(const Endpoint& ep) = 0;

    static std::unique_ptr<UdpMultiPeerSocket> create(AOContext& aoCtx, const Params& params);
    // wraps already prepared socket
    static std::unique_ptr<UdpMultiPeerSocket> create(AOContext& aoCtx, NativeHandle native);
};

}   // namespace nhope
