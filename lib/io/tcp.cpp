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
#include "nhope/io/sock-addr.h"
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

    explicit TcpSocketImpl(nhope::AOContext& parent, NativeHandle handle)
      : detail::AsioDeviceWrapper<TcpSocket, AsioSocket>(parent)
    {
        asioDev.assign(asio::ip::tcp::v4(), static_cast<int>(handle));
    }

    [[nodiscard]] SockAddr localAddress() const override
    {
        const auto endpoint = this->asioDev.local_endpoint();
        return SockAddr{endpoint.data(), endpoint.size()};
    }

    [[nodiscard]] SockAddr peerAddress() const override
    {
        const auto endpoint = this->asioDev.remote_endpoint();
        return SockAddr{endpoint.data(), endpoint.size()};
    }

    [[nodiscard]] NativeHandle nativeHandle() override
    {
        return this->asioDev.native_handle();
    }

    void setOptions(const Options& opts) override
    {
        if (opts.keepAlive.has_value()) {
            const asio::ip::tcp::socket::keep_alive keep(opts.keepAlive.value());
            this->asioDev.set_option(keep);
        }
        if (opts.reuseAddress.has_value()) {
            const asio::ip::tcp::socket::reuse_address reuse(opts.reuseAddress.value());
            this->asioDev.set_option(reuse);
        }
        if (opts.receiveBufferSize.has_value()) {
            const asio::socket_base::receive_buffer_size option(opts.receiveBufferSize.value());
            this->asioDev.set_option(option);
        }
        if (opts.sendBufferSize.has_value()) {
            const asio::socket_base::send_buffer_size option(opts.sendBufferSize.value());
            this->asioDev.set_option(option);
        }
        this->asioDev.non_blocking(opts.nonBlocking);
    }

    [[nodiscard]] Options options() const override
    {
        Options opts;
        asio::socket_base::send_buffer_size sendOpt;
        this->asioDev.get_option(sendOpt);
        opts.sendBufferSize = sendOpt.value();
        asio::socket_base::receive_buffer_size receiveSizeOpt;
        this->asioDev.get_option(receiveSizeOpt);
        opts.receiveBufferSize = receiveSizeOpt.value();

        asio::ip::tcp::socket::keep_alive keepOpt;
        this->asioDev.get_option(keepOpt);
        opts.keepAlive = keepOpt.value();

        asio::ip::tcp::socket::reuse_address reuseOpt;
        this->asioDev.get_option(reuseOpt);
        opts.reuseAddress = reuseOpt.value();

        return opts;
    }

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

    [[nodiscard]] SockAddr bindAddress() const override
    {
        const auto endpoint = this->m_acceptor.local_endpoint();
        return SockAddr{endpoint.data(), endpoint.size()};
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
        m_aoCtx.startCancellableTask(
          [&] {
              const auto service = std::to_string(port);

              m_resolver.async_resolve(hostName, service, [this, aoCtx = m_aoCtx](auto err, auto results) mutable {
                  aoCtx.exec(
                    [this, err, results = std::move(results)] {
                        this->resolveHandler(err, results);
                    },
                    Executor::ExecMode::ImmediatelyIfPossible);
              });
          },
          *this);
    }

    ~ConnectOp() override
    {
        m_aoCtx.removeCloseHandler(*this);
    }

private:
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

TcpSocketPtr TcpSocket::create(AOContext& aoCtx, NativeHandle handle)
{
    return std::make_unique<TcpSocketImpl>(aoCtx, handle);
}

TcpServerPtr TcpServer::start(AOContext& aoCtx, const TcpServerParams& params)
{
    return std::make_unique<TcpServerImpl>(aoCtx, params);
}

}   // namespace nhope
