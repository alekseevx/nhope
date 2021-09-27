#pragma once

#include <memory>
#include <utility>

#include "nhope/async/ao-context.h"
#include "nhope/async/async-invoke.h"
#include "nhope/async/thread-executor.h"
#include "nhope/io/io-device.h"
#include "nhope/io/tcp.h"

namespace nhope::test {

class TcpEchoServer final
{
public:
    static constexpr auto portionSize = 1024;
    static constexpr auto srvPort = 55577;
    static constexpr auto srvAddress = "127.0.0.1";

    explicit TcpEchoServer()
      : m_ctx(m_thread)
      , m_tcpServer(listen(m_ctx, {srvAddress, srvPort}))
    {
        asyncInvoke(m_ctx, [this] {
            this->acceptNextClient();
        });
    }

    ~TcpEchoServer()
    {
        m_ctx.close();
    }

private:
    void acceptNextClient()
    {
        m_tcpServer->accept().then(m_ctx, [this](TcpSocketPtr client) mutable {
            this->startSession(std::move(client));
            this->acceptNextClient();
        });
    }

    void startSession(TcpSocketPtr sock)
    {
        this->startRead(std::move(sock));
    }

    void startRead(const std::shared_ptr<TcpSocket>& sock)
    {
        read(*sock, portionSize)
          .then(m_ctx,
                [sock](auto data) {
                    return write(*sock, std::move(data));
                })
          .then(m_ctx, [this, sock](auto /*unused*/) {
              this->startRead(sock);
          });
    }

    ThreadExecutor m_thread;
    AOContext m_ctx;
    std::unique_ptr<TcpServer> m_tcpServer;
};

}   // namespace nhope::test
