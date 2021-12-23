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
#include "nhope/utils/scope-exit.h"

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

class ConnectOp final : public AOContextCloseHandler
{
public:
    explicit ConnectOp(AOContext& aoCtx, Promise<TcpSocketPtr>&& promise, std::string_view hostName, std::uint16_t port)
      : m_aoCtx(aoCtx)
      , m_resolver(aoCtx.executor().ioCtx())
      , m_promise(std::move(promise))
    {
        this->start(hostName, port);
        m_aoCtx.addCloseHandler(*this);
    }

    ~ConnectOp()
    {
        m_aoCtx.removeCloseHandler(*this);
    }

private:
    void start(std::string_view hostName, std::uint16_t port)
    {
        const auto service = std::to_string(port);

        m_resolver.async_resolve(hostName, service, [this, aoCtx = m_aoCtx](auto err, auto results) mutable {
            aoCtx.exec(
              [this, err, results = std::move(results)] {
                  this->resolveHandler(err, results);
              },
              Executor::ExecMode::ImmediatelyIfPossible);
        });
    }

    void aoContextClose() noexcept override
    {
        this->cancel();
        delete this;
    }

    void cancel()
    {
        m_resolver.cancel();
        m_socket.reset();

        m_promise.setException(std::make_exception_ptr(AsyncOperationWasCancelled()));
    }

    void resolveHandler(std::error_code err, const ResolveResults& results)
    {
        if (err) {
            m_promise.setException(std::make_exception_ptr(std::system_error(err)));
            delete this;
            return;
        }

        m_socket = std::make_unique<TcpSocketImpl>(m_aoCtx);

        auto& asioSocket = m_socket->asioDev;
        asioSocket.async_connect(results->endpoint(), [this, aoCtx = m_aoCtx](auto err) mutable {
            aoCtx.exec(
              [this, err] {
                  this->connectHandler(err);
              },
              Executor::ExecMode::ImmediatelyIfPossible);
        });
    }

    void connectHandler(const std::error_code& err)
    {
        ScopeExit clear([this] {
            delete this;
        });

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
    auto [future, promise] = makePromise<TcpSocketPtr>();
    new ConnectOp(aoCtx, std::move(promise), hostName, port);
    return std::move(future);
}

TcpServerPtr TcpServer::start(AOContext& aoCtx, const TcpServerParams& params)
{
    return std::make_unique<TcpServerImpl>(aoCtx, params);
}

}   // namespace nhope
