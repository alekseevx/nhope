#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "nhope/utils/detail/fast-pimpl.h"

struct sockaddr;
struct sockaddr_storage;

namespace nhope {

class SockAddr final
{
public:
    SockAddr(const struct sockaddr* sockaddr, std::size_t size);
    ~SockAddr();

    std::optional<std::uint16_t> port() const noexcept;

    std::pair<const struct sockaddr*, std::size_t> native() const;
    std::string toString() const;

    static SockAddr ipv4(std::string_view ip, std::uint16_t port);

private:
    static constexpr auto maxSockaddrSize = 128;
    detail::FastPimpl<sockaddr_storage, maxSockaddrSize> m_impl;
};

}   // namespace nhope
