
#include <exception>
#include <memory>
#include <string>
#include <system_error>
#include <utility>

#include <asio/ip/tcp.hpp>
#include "asio/any_io_executor.hpp"
#include "asio/connect.hpp"
#include "asio/error_code.hpp"
#include "asio/io_context.hpp"
#include "asio/post.hpp"
#include "nhope/async/executor.h"
#include "nhope/io/tcp.h"
#include "nhope/async/ao-context.h"
#include "nhope/io/detail/asio-device.h"
#include "nhope/seq/consumer-list.h"

namespace nhope {

TcpError::TcpError(std::error_code errCode)
  : IoError(errCode)
{}

namespace {

using TcpDevice = detail::AsioDevice<asio::ip::tcp::socket>;

class TcpServerImpl final : public TcpServer
{
public:
    explicit TcpServerImpl(Executor& e, const TcpServerParam& settings)
      : m_executor(e)
      , m_acceptor(e.ioCtx(), asio::ip::tcp::endpoint(asio::ip::tcp::v4(), settings.endpoint.port))
    {}

    Future<IoDevicePtr> accept() override
    {
        Promise<IoDevicePtr> promise;
        auto res = promise.future();
        startAccept(std::move(promise));
        return res;
    }

private:
    void startAccept(Promise<IoDevicePtr> promise)
    {
        auto dev = std::make_unique<TcpDevice>(m_executor);
        auto& impl = dev->impl();
        m_acceptor.async_accept(impl,
                                [d = std::move(dev), promise = std::move(promise)](const auto& err) mutable {
                                    if (!err) {
                                        promise.setValue(std::move(d));
                                        return;
                                    }
                                    promise.setException(std::make_exception_ptr(TcpError(err)));
                                }

        );
    }

    Executor& m_executor;
    asio::ip::tcp::acceptor m_acceptor;
};

void configureClient(Executor& e, const TcpClientParam& s, const std::shared_ptr<Promise<std::unique_ptr<IoDevice>>>& p)
{
    auto dev = std::make_unique<TcpDevice>(e);
    auto& socket = dev->impl();
    auto resolver = std::make_shared<asio::ip::tcp::resolver>(dev->executor().ioCtx());
    // TODO  set options...
    // asio::ip::tcp::no_delay
    // socket.set_option();

    resolver->async_resolve(
      s.endpoint.host, std::to_string(s.endpoint.port),
      [dev = std::move(dev), &socket, p, resolver](const asio::error_code& errCode, auto results) mutable {
          if (errCode) {
              p->setException(std::make_exception_ptr(TcpError(errCode)));
              return;
          }

          asio::async_connect(
            socket, results,
            [dev = std::move(dev), p](const auto& errCode, const asio::ip::tcp::endpoint& /*unused*/) mutable {
                if (errCode) {
                    p->setException(std::make_exception_ptr(TcpError(errCode)));
                    return;
                }
                p->setValue(std::move(dev));
            });
      });
}

}   // namespace

Future<std::unique_ptr<IoDevice>> connect(Executor& e, const TcpClientParam& settings)
{
    auto promise = std::make_shared<Promise<std::unique_ptr<IoDevice>>>();
    auto res = promise->future();
    asio::post(e.ioCtx(), [&, settings, promise]() {
        configureClient(e, settings, promise);
    });
    return res;
}

std::unique_ptr<TcpServer> listen(Executor& e, const TcpServerParam& settings)
{
    return std::make_unique<TcpServerImpl>(e, settings);
}

}   // namespace nhope