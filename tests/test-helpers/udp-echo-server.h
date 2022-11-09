#pragma once

#include <cstdint>
#include <gtest/gtest.h>
#include <memory>
#include <utility>

#include "nhope/async/ao-context.h"
#include "nhope/async/async-invoke.h"
#include "nhope/async/thread-executor.h"
#include "nhope/io/io-device.h"
#include "nhope/io/udp.h"

namespace nhope::test {

class UdpEchoServer final
{
public:
    explicit UdpEchoServer(const UdpSocket::Params& p)
      : m_ctx(m_thread)
      , m_socket(UdpSocket::create(m_ctx, p))
    {
        EXPECT_TRUE(m_socket->nativeHandle() != 0);
        asyncInvoke(m_ctx, [this] {
            this->readNext();
        });
    }

    ~UdpEchoServer()
    {
        m_ctx.close();
    }

    SockAddr peer()
    {
        return m_socket->peerAddress();
    }

    std::uint16_t bindPort()
    {
        return *m_socket->localAddress().port();
    }

private:
    void readNext()
    {
        constexpr auto size{512};
        nhope::read(*m_socket, size)
          .then(m_ctx,
                [this](auto data) {
                    return nhope::write(*m_socket, data);
                })
          .then(m_ctx,
                [this](auto) {
                    readNext();
                })
          .fail(m_ctx, [this](auto) {
              readNext();
          });
    }

private:
    ThreadExecutor m_thread;
    AOContext m_ctx;
    UdpSocketPtr m_socket;
};

}   // namespace nhope::test
