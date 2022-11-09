
#include <asio/ip/udp.hpp>

#include "asio/ip/address.hpp"
#include "asio/ip/address_v4.hpp"
#include "nhope/io/udp.h"
#include "nhope/io/detail/asio-device-wrapper.h"

namespace nhope {

using AsioSocket = asio::ip::udp::socket;

namespace {

class UdpSocketImpl final : public UdpSocket
{
public:
    explicit UdpSocketImpl(nhope::AOContext& parentAOCtx, const UdpSocketImpl::Params& params)
      : m_socket(parentAOCtx.executor().ioCtx())
      , m_aoCtx(parentAOCtx)
    {
        m_socket.open(asio::ip::udp::v4());
        setOptions(params);
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

private:
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
    }

private:
    asio::ip::udp::endpoint m_endpoint;
    AsioSocket m_socket;
    AOContext m_aoCtx;
};
}   // namespace

UdpSocketPtr UdpSocket::create(AOContext& aoCtx, const UdpSocketImpl::Params& params)
{
    return std::make_unique<UdpSocketImpl>(aoCtx, params);
}

}   // namespace nhope
