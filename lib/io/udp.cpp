
#include <algorithm>
#include <asio/ip/udp.hpp>
#include <cstddef>
#include <exception>
#include <stdexcept>
#include <utility>
#include <vector>

#include "asio/ip/address.hpp"
#include "asio/ip/address_v4.hpp"
#include "nhope/io/udp.h"
#include "nhope/async/all.h"
#include "nhope/async/async-invoke.h"
#include "nhope/async/future.h"
#include "nhope/io/detail/asio-device-wrapper.h"
#include "nhope/io/io-device.h"

namespace nhope {

using AsioSocket = asio::ip::udp::socket;
namespace {

asio::ip::udp::endpoint fromEndpoint(const UdpSocket::Endpoint& ep)
{
    return asio::ip::udp::endpoint(asio::ip::address_v4::from_string(ep.address), ep.port);
}

class UdpSocketImpl : virtual public UdpSocket
{
public:
    explicit UdpSocketImpl(nhope::AOContext& parentAOCtx, const UdpSocketImpl::Params& params)
      : m_socket(parentAOCtx.executor().ioCtx())
      , m_aoCtx(parentAOCtx)
    {
        m_socket.open(asio::ip::udp::v4());
        setOptions(params);
    }

    explicit UdpSocketImpl(nhope::AOContext& parentAOCtx, NativeHandle handle)
      : m_socket(parentAOCtx.executor().ioCtx())
      , m_aoCtx(parentAOCtx)
    {
        m_socket.assign(asio::ip::udp::v4(), static_cast<int>(handle));
    }

    ~UdpSocketImpl() override = default;

    [[nodiscard]] NativeHandle nativeHandle() override
    {
        return this->m_socket.native_handle();
    }

    [[nodiscard]] SockAddr localAddress() const override
    {
        const auto endpoint = this->m_socket.local_endpoint();
        return SockAddr{endpoint.data(), endpoint.size()};
    }

    [[nodiscard]] SockAddr peerAddress() const override
    {
        return SockAddr{m_endpoint.data(), m_endpoint.size()};
    }

    void ioCancel() override
    {
        m_socket.cancel();
    }

    void read(gsl::span<std::uint8_t> buf, IOHandler handler) override
    {
        m_socket.async_receive_from(
          asio::buffer(buf.data(), buf.size()), m_endpoint,
          [aoCtx = AOContextRef(m_aoCtx), handler = std::move(handler)](auto err, auto count) mutable {
              aoCtx.exec(
                [handler = std::move(handler), err, count] {
                    handler(detail::toExceptionPtr(err), count);
                },
                Executor::ExecMode::ImmediatelyIfPossible);
          });
    }

    void write(gsl::span<const std::uint8_t> data, IOHandler handler) override
    {
        m_socket.async_send_to(
          asio::buffer(data.data(), data.size()), m_endpoint,
          [aoCtx = AOContextRef(m_aoCtx), handler = std::move(handler)](auto& err, auto count) mutable {
              aoCtx.exec(
                [handler = std::move(handler), err, count] {
                    handler(detail::toExceptionPtr(err), count);
                },
                Executor::ExecMode::ImmediatelyIfPossible);
          });
    }

protected:
    void setOptions(const UdpSocketImpl::Params& opts)
    {
        asio::ip::address_v4 ip = asio::ip::address_v4::any();
        if (!opts.bindAddress.address.empty()) {
            ip = asio::ip::address_v4::from_string(opts.bindAddress.address);
        }
        m_socket.bind(asio::ip::udp::endpoint(ip, opts.bindAddress.port));

        if (opts.peerAddress.has_value()) {
            m_endpoint = asio::ip::udp::endpoint(asio::ip::address_v4::from_string(opts.peerAddress->address),
                                                 opts.peerAddress->port);
        }

        if (opts.broadcast.has_value()) {
            const asio::ip::udp::socket::broadcast broad(opts.broadcast.value());
            m_socket.set_option(broad);
        }
        if (opts.reuseAddress.has_value()) {
            const asio::ip::udp::socket::reuse_address reuse(opts.reuseAddress.value());
            m_socket.set_option(reuse);
        }
        if (opts.receiveBufferSize.has_value()) {
            const asio::socket_base::receive_buffer_size option(opts.receiveBufferSize.value());
            m_socket.set_option(option);
        }
        if (opts.sendBufferSize.has_value()) {
            const asio::socket_base::send_buffer_size option(opts.sendBufferSize.value());
            m_socket.set_option(option);
        }
        m_socket.non_blocking(opts.nonBlocking);
    }

protected:
    asio::ip::udp::endpoint m_endpoint;
    AsioSocket m_socket;
    mutable AOContext m_aoCtx;
};

class UdpMultiPeerSocketImpl final
  : virtual public UdpSocketImpl
  , virtual public UdpMultiPeerSocket
{
public:
    explicit UdpMultiPeerSocketImpl(nhope::AOContext& parentAOCtx, const UdpSocketImpl::Params& params)
      : UdpSocketImpl(parentAOCtx, params)
    {
        if (m_endpoint.port() != 0) {
            m_peers.emplace_back(Endpoint{m_endpoint.address().to_string(), std::uint16_t(m_endpoint.port())});
        }
    }

    explicit UdpMultiPeerSocketImpl(nhope::AOContext& parentAOCtx, NativeHandle handle)
      : UdpSocketImpl(parentAOCtx, handle)
    {}

    void write(gsl::span<const std::uint8_t> data, IOHandler handler) override
    {
        m_aoCtx.exec(
          [this, data, handler = std::move(handler)] {
              if (m_peers.empty()) {
                  handler(nullptr, data.size());
                  return;
              }
              all(
                m_aoCtx,
                [this, data = asio::buffer(data.data(), data.size())](AOContext&, const Endpoint& ctx) {
                    auto p = makePromise<std::size_t>();
                    m_socket.async_send_to(data, fromEndpoint(ctx),
                                           [promise = std::move(p.second)](auto& err, auto count) mutable {
                                               if (err) {
                                                   promise.setException(detail::toExceptionPtr(err));
                                                   return;
                                               }
                                               promise.setValue(count);
                                           });

                    return std::move(p.first);
                },
                m_peers)
                .then(m_aoCtx,
                      [handler](std::vector<std::size_t> sizes) {
                          handler(nullptr, sizes.front());
                      })
                .fail(m_aoCtx, [handler](auto ex) {
                    handler(std::move(ex), 0);
                });
          },
          Executor::ExecMode::ImmediatelyIfPossible);
    }

    // peer list for resending
    [[nodiscard]] std::vector<Endpoint> peers() const noexcept override
    {
        return nhope::invoke(m_aoCtx, [this] {
            return m_peers;
        });
    }

    // add peer for resending
    void addPeer(Endpoint ep) override
    {
        nhope::asyncInvoke(m_aoCtx, [this, ep = std::move(ep)]() mutable {
            m_peers.emplace_back(std::move(ep));
        });
    }

    void removePeer(const Endpoint& ep) override
    {
        nhope::asyncInvoke(m_aoCtx, [this, ep]() mutable {
            if (auto it = std::find(m_peers.begin(), m_peers.end(), ep); it != m_peers.end()) {
                // item sequence doesn`t matter
                *it = std::move(m_peers.back());
                m_peers.resize(m_peers.size() - 1);
            }
        });
    }

private:
    std::vector<Endpoint> m_peers;
};
}   // namespace

UdpSocketPtr UdpSocket::create(AOContext& aoCtx, const UdpSocketImpl::Params& params)
{
    return std::make_unique<UdpSocketImpl>(aoCtx, params);
}

UdpSocketPtr UdpSocket::create(AOContext& aoCtx, NativeHandle native)
{
    return std::make_unique<UdpSocketImpl>(aoCtx, native);
}

std::unique_ptr<UdpMultiPeerSocket> UdpMultiPeerSocket::create(AOContext& aoCtx, const Params& params)
{
    return std::make_unique<UdpMultiPeerSocketImpl>(aoCtx, params);
}

std::unique_ptr<UdpMultiPeerSocket> UdpMultiPeerSocket::create(AOContext& aoCtx, NativeHandle native)
{
    return std::make_unique<UdpMultiPeerSocketImpl>(aoCtx, native);
}

}   // namespace nhope
