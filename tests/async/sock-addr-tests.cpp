#ifdef WIN32
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include <WinSock2.h>
#else
#include <sys/socket.h>
#include <netinet/in.h>
#endif   // WIN32

#include <memory>
#include <stdexcept>
#include <system_error>

#include "gtest/gtest.h"
#include "nhope/io/sock-addr.h"

using namespace nhope;

TEST(SockAddr, ConstructionWithInvalidSockaddrLen)   // NOLINT
{
    {
        sockaddr nativeSockaddr{};
        nativeSockaddr.sa_family = AF_UNSPEC;
        EXPECT_THROW(SockAddr(&nativeSockaddr, 1), std::runtime_error);      // NOLINT
        EXPECT_THROW(SockAddr(&nativeSockaddr, 1024), std::runtime_error);   // NOLINT
    }

    {
        sockaddr nativeSockaddr{};
        nativeSockaddr.sa_family = AF_INET;
        EXPECT_THROW(SockAddr(&nativeSockaddr, sizeof(sockaddr_in) - 1), std::runtime_error);   // NOLINT
    }
}

TEST(SockAddr, Native)   // NOLINT
{
    {
        sockaddr_storage native{};
        native.ss_family = {AF_UNSPEC};

        // NOLINTNEXTLINE
        SockAddr sockAddr(&reinterpret_cast<sockaddr&>(native), 20);
        EXPECT_EQ(sockAddr.native().first->sa_family, AF_UNSPEC);
        EXPECT_EQ(sockAddr.native().second, sizeof(sockaddr_storage));
    }

    {
        sockaddr_storage native{};
        native.ss_family = {AF_INET};
        // NOLINTNEXTLINE
        SockAddr sockAddr(&reinterpret_cast<sockaddr&>(native), sizeof(sockaddr_storage));
        EXPECT_EQ(sockAddr.native().first->sa_family, AF_INET);
        EXPECT_EQ(sockAddr.native().second, sizeof(sockaddr_in));
    }
}

TEST(SockAddr, IPv4Create)   // NOLINT
{
    const auto sa = SockAddr::ipv4("127.0.0.1", 4567);
    const auto [sockaddr, sockaddrSize] = sa.native();
    EXPECT_EQ(sockaddrSize, sizeof(sockaddr_in));

    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    const auto* sockaddrIn = reinterpret_cast<const sockaddr_in*>(sockaddr);
    EXPECT_EQ(ntohs(sockaddrIn->sin_port), 4567);   // NOLINT
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-union-acces)
    EXPECT_EQ(ntohl(sockaddrIn->sin_addr.s_addr), INADDR_LOOPBACK);

    EXPECT_THROW(SockAddr::ipv4("InvalidIP", 4567), std::system_error);   // NOLINT
}

TEST(SockAddr, IPv4ToString)   // NOLINT
{
    const auto sa = SockAddr::ipv4("127.0.0.1", 4567);
    EXPECT_EQ(sa.toString(), "127.0.0.1:4567");
}

TEST(SockAddr, ToStringFail)   // NOLINT
{
    sockaddr nativeSockaddr{};
    nativeSockaddr.sa_family = AF_UNSPEC;
    SockAddr sockaddr(&nativeSockaddr, sizeof(nativeSockaddr));

    EXPECT_THROW(sockaddr.toString(), std::runtime_error);   // NOLINT
}
