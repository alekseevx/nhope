#include "nhope/io/local-socket.h"
#include "nhope/async/ao-context.h"
#include <asio/local/stream_protocol.hpp>
#include <filesystem>
#include <memory>
#include <string>
#include <system_error>
#include "nhope/io/detail/asio-device-wrapper.h"

namespace nhope {

namespace {

using AsioSocket = asio::local::stream_protocol::socket;

class LocalSocketImpl final : public detail::AsioDeviceWrapper<LocalSocket, AsioSocket>
{
public:
    explicit LocalSocketImpl(nhope::AOContext& parent)
      : detail::AsioDeviceWrapper<LocalSocket, AsioSocket>(parent)
    {}
};

class LocalServerImpl final
  : public LocalServer
  , public AOContextCloseHandler
{
public:
    explicit LocalServerImpl(AOContext& aoCtx, const LocalServerParams& params)
      : m_acceptor(aoCtx.executor().ioCtx(), asio::local::stream_protocol::endpoint(params.address))
      , m_filename(params.address)
      , m_aoCtx(aoCtx)
    {
        m_aoCtx.addCloseHandler(*this);
    }

    ~LocalServerImpl() override
    {
        m_aoCtx.removeCloseHandler(*this);

        std::error_code er;
        std::filesystem::remove(m_filename, er);
    }

    Future<std::unique_ptr<LocalSocket>> accept() override
    {
        Promise<std::unique_ptr<LocalSocket>> promise;
        auto future = promise.future();
        auto newClient = std::make_unique<LocalSocketImpl>(m_aoCtx);
        auto& asioSocket = newClient->asioDev;
        m_acceptor.async_accept(asioSocket,
                                [newClient = std::move(newClient), p = std::move(promise)](auto err) mutable {
                                    if (err) {
                                        p.setException(std::make_exception_ptr(std::system_error(err)));
                                        return;
                                    }
                                    p.setValue(std::move(newClient));
                                });
        return future;
    }

private:
    void aoContextClose() noexcept override
    {
        m_acceptor.close();
    }

private:
    asio::local::stream_protocol::acceptor m_acceptor;
    const std::string m_filename;
    AOContext m_aoCtx;
};

class ConnectOp final : public AOContextCloseHandler
{
public:
    explicit ConnectOp(AOContext& aoCtx, Promise<std::unique_ptr<LocalSocket>>&& promise, std::string_view address)
      : m_aoCtx(aoCtx)
      , m_promise(std::move(promise))
      , m_socket(std::make_unique<LocalSocketImpl>(aoCtx))
    {
        m_aoCtx.startCancellableTask(
          [this, address]() mutable {
              auto& asioSocket = m_socket->asioDev;
              asioSocket.async_connect(asio::local::stream_protocol::endpoint(address),
                                       [this, aoCtx = m_aoCtx](auto err) mutable {
                                           aoCtx.exec(
                                             [this, err] {
                                                 this->connectHandler(err);
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

    void cancel()
    {
        m_socket.reset();
        m_promise.setException(std::make_exception_ptr(AsyncOperationWasCancelled()));
    }

private:
    AOContextRef m_aoCtx;
    Promise<std::unique_ptr<LocalSocket>> m_promise;
    std::unique_ptr<LocalSocketImpl> m_socket;
};

}   // namespace

Future<std::unique_ptr<LocalSocket>> LocalSocket::connect(AOContext& aoCtx, std::string_view address)
{
    auto [future, promise] = makePromise<std::unique_ptr<LocalSocket>>();
    new ConnectOp(aoCtx, std::move(promise), address);
    return std::move(future);
}

std::unique_ptr<LocalServer> LocalServer::start(AOContext& aoCtx, const LocalServerParams& params)
{
    return std::make_unique<LocalServerImpl>(aoCtx, params);
}

}   // namespace nhope
