#ifdef WIN32
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include <WinSock2.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#endif   // WIN32

#include <cerrno>
#include <cstring>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>

#include "fmt/core.h"

#include "nhope/io/sock-addr.h"

namespace nhope {

namespace {

std::string ntoa(const struct sockaddr_in& sa)
{
    const auto* ip = inet_ntoa(sa.sin_addr);
    const auto port = htons(sa.sin_port);
    return fmt::format("{}:{}", ip, port);
}

}   // namespace

SockAddr::SockAddr(const struct sockaddr* sockaddr, std::size_t size)
{
    if (size < sizeof(sockaddr->sa_family)) {
        throw std::runtime_error("The sockaddr is too small");
    }

    if (size > maxSockaddrSize) {
        throw std::runtime_error("The sockaddr is too large");
    }

    if (sockaddr->sa_family == AF_INET && size < sizeof(sockaddr_in)) {
        throw std::runtime_error("The sockaddr is not large enough for AF_INET");
    }

    std::memcpy(m_impl.get(), sockaddr, size);
}

SockAddr::~SockAddr() = default;

std::pair<const struct sockaddr*, std::size_t> SockAddr::native() const
{
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    const auto* sockaddr = reinterpret_cast<const struct sockaddr*>(m_impl.get());
    switch (m_impl->ss_family) {
    case AF_INET:
        return {sockaddr, sizeof(sockaddr_in)};

    default:
        return {sockaddr, maxSockaddrSize};
    }
}

std::string SockAddr::toString() const
{
    switch (m_impl->ss_family) {
    case AF_INET:
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
        return ntoa(reinterpret_cast<const struct sockaddr_in&>(*m_impl));

    default:
        throw std::runtime_error("Unsupported address family");
    }
}

SockAddr SockAddr::ipv4(std::string_view ip, std::uint16_t port)
{
    std::string cip(ip);   // C-string for inet_aton

    sockaddr_in sin{AF_INET};
    sin.sin_port = htons(port);
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-union-acces)
    sin.sin_addr.s_addr = inet_addr(cip.c_str());
    if (sin.sin_addr.s_addr == INADDR_NONE) {
        throw std::system_error{
          std::error_code(errno, std::system_category()),
          "Unable to convert string to IPv4",
        };
    }

    return SockAddr{
      // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
      reinterpret_cast<const struct sockaddr*>(&sin),
      sizeof(sin),
    };
}

}   // namespace nhope
