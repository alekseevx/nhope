#include <exception>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#include <asio/connect.hpp>
#include <asio/ip/tcp.hpp>

#include "nhope/async/ao-context-close-handler.h"
#include "nhope/async/ao-context-error.h"
#include "nhope/async/ao-context.h"
#include "nhope/async/executor.h"
#include "nhope/async/future.h"
#include "nhope/async/safe-callback.h"
#include "nhope/io/detail/asio-device-wrapper.h"
#include "nhope/io/tcp.h"

namespace nhope {

namespace {

using AsioSocket = asio::ip::tcp::socket;
using Endpoint = asio::ip::tcp::endpoint;
using Resolver = asio::ip::tcp::resolver;
using ResolveResults = Resolver::results_type;

asio::ip::tcp::socket::shutdown_type toAsioShutdown(TcpSocket::Shutdown what)
{
    switch (what) {
    case TcpSocket::Shutdown::Receive:
        return asio::ip::tcp::socket::shutdown_type::shutdown_receive;
    case TcpSocket::Shutdown::Send:
        return asio::ip::tcp::socket::shutdown_type::shutdown_send;
    default:
        return asio::ip::tcp::socket::shutdown_type::shutdown_both;
    }
}

class TcpSocketImpl final : public detail::AsioDeviceWrapper<TcpSocket, AsioSocket>
{
public:
    explicit TcpSocketImpl(nhope::AOContextRef& parent)
      : detail::AsioDeviceWrapper<TcpSocket, AsioSocket>(parent)
    {}

    void shutdown(Shutdown what) override
    {
        this->asioDev.shutdown(toAsioShutdown(what));
    }
};

class TcpServerImpl final
  : public TcpServer
  , public AOContextCloseHandler
{
public:
    explicit TcpServerImpl(AOContext& aoCtx, const TcpServerParams& params)
      : m_acceptor(aoCtx.executor().ioCtx(), asio::ip::tcp::endpoint(asio::ip::tcp::v4(), params.port))
      , m_aoCtx(aoCtx)
    {
        m_aoCtx.addCloseHandler(*this);
    }

    ~TcpServerImpl() override
    {
        m_aoCtx.removeCloseHandler(*this);
    }

    Future<TcpSocketPtr> accept() override
    {
        Promise<TcpSocketPtr> promise;
        auto future = promise.future();

        auto newClient = std::make_unique<TcpSocketImpl>(m_aoCtx);

        auto& asioSocket = newClient->asioDev;
        m_acceptor.async_accept(asioSocket,
                                [newClient = std::move(newClient), p = std::move(promise)](auto err) mutable {
                                    if (err) {
                                        p.setException(std::make_exception_ptr(std::system_error(err)));
                                        return;
                                    }

                                    p.setValue(std::move(newClient));
                                }

        );

        return future;
    }

private:
    void aoContextClose() noexcept override
    {
        m_acceptor.close();
    }

    asio::ip::tcp::acceptor m_acceptor;
    AOContextRef m_aoCtx;
};

class ConnectOp final
  : public AOContextCloseHandler
  , public std::enable_shared_from_this<ConnectOp>
{
public:
    explicit ConnectOp(AOContext& aoCtx)
      : m_aoCtx(aoCtx)
      , m_resolver(aoCtx.executor().ioCtx())
    {
        m_aoCtx.addCloseHandler(*this);
    }

    ~ConnectOp()
    {
        m_aoCtx.removeCloseHandler(*this);
    }

    Future<TcpSocketPtr> start(std::string_view hostName, std::uint16_t port)
    {
        const auto service = std::to_string(port);

        auto callback = makeSafeCallback(
          m_aoCtx,
          [self = shared_from_this()](const std::error_code& err, const ResolveResults& results) {
              self->resolveHandler(err, results);
          },
          NotThrowAOContextClosed);

        m_resolver.async_resolve(hostName, service, std::move(callback));

        return m_promise.future();
    }

private:
    void aoContextClose() noexcept override
    {
        this->cancel();
    }

    void cancel()
    {
        if (!m_promise.satisfied()) {
            m_resolver.cancel();
            m_socket.reset();

            m_promise.setException(std::make_exception_ptr(AsyncOperationWasCancelled()));
        }
    }

    void resolveHandler(const std::error_code& err, const ResolveResults& results)
    {
        if (err) {
            m_promise.setException(std::make_exception_ptr(std::system_error(err)));
            return;
        }

        m_socket = std::make_unique<TcpSocketImpl>(m_aoCtx);

        auto callback = makeSafeCallback(
          m_aoCtx,
          [self = shared_from_this()](const std::error_code& err) {
              self->connectHandler(err);
          },
          NotThrowAOContextClosed);
        auto& asioSocket = m_socket->asioDev;
        asioSocket.async_connect(results->endpoint(), std::move(callback));
    }

    void connectHandler(const std::error_code& err)
    {
        if (err) {
            m_promise.setException(std::make_exception_ptr(std::system_error(err)));
            return;
        }

        m_promise.setValue(std::move(m_socket));
    }

    AOContextRef m_aoCtx;

    Resolver m_resolver;
    std::unique_ptr<TcpSocketImpl> m_socket;
    Promise<TcpSocketPtr> m_promise;
};

}   // namespace

Future<TcpSocketPtr> TcpSocket::connect(AOContext& aoCtx, std::string_view hostName, std::uint16_t port)
{
    auto connectOp = std::make_shared<ConnectOp>(aoCtx);
    return connectOp->start(hostName, port);
}

TcpServerPtr TcpServer::start(AOContext& aoCtx, const TcpServerParams& params)
{
    return std::make_unique<TcpServerImpl>(aoCtx, params);
}

}   // namespace nhope
