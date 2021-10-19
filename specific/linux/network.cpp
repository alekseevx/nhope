#include <algorithm>
#include <array>
#include <cassert>
#include <cstdio>
#include <fstream>
#include <string>
#include <cstring>

#include <ifaddrs.h>
#include <netdb.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "nhope/io/network.h"
#include "nhope/utils/scope-exit.h"

namespace nhope {

namespace {

using namespace std::literals;

struct RouteIface
{
    std::string iface;
    std::string route;
};

std::string getIpV4(const struct sockaddr* adr)
{
    assert(adr->sa_family == AF_INET);   //NOLINT
    std::array<char, NI_MAXHOST> host{};
    getnameinfo(adr, sizeof(sockaddr_in), host.data(), NI_MAXHOST, nullptr, 0, NI_NUMERICHOST);
    return host.data();
}

RouteIface getGateway()
{
    std::array<char, NI_MAXHOST> iface{};
    std::string gate = "0.0.0.0";
    std::ifstream fs("/proc/net/route");
    std::string gateLine;
    while (std::getline(fs, gateLine)) {
        uint32_t destination{};
        uint32_t gateway{};
        if (sscanf(gateLine.c_str(), "%s %x %x", iface.data(), &destination, &gateway) == 3) {
            if (destination == 0) {
                gate = inet_ntoa(*(struct in_addr*)&gateway);
                break;
            }
        }
    }
    return {iface.data(), gate};
}

}   // namespace

/**Получить локальный IP-адрес.*/
std::string getLocalIp()
{
    struct ifaddrs* ifaddr = nullptr;
    struct ifaddrs* ifa = nullptr;
    if (getifaddrs(&ifaddr) == 0) {
        ScopeExit addrCloser([ifaddr] {
            freeifaddrs(ifaddr);
        });

        for (ifa = ifaddr; ifa != nullptr; ifa = ifa->ifa_next) {
            if (ifa->ifa_addr != nullptr) {
                if (ifa->ifa_addr->sa_family == AF_INET) {
                    if (strcmp("lo", ifa->ifa_name) != 0) {
                        return getIpV4(ifa->ifa_addr);
                    }
                }
            }
        }
    }

    return "127.0.0.1";
}

std::vector<AddressEntry> addressEntries()
{
    using namespace std::literals;
    ifaddrs* ifaddr = nullptr;
    ifaddrs* ifa = nullptr;
    std::string iface;
    std::string ip;
    std::vector<AddressEntry> result;

    if (getifaddrs(&ifaddr) == -1) {
        return result;
    }
    ScopeExit addrCloser([ifaddr] {
        freeifaddrs(ifaddr);
    });

    const auto gate = getGateway().route;

    for (ifa = ifaddr; ifa != nullptr; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr != nullptr) {
            if (strcmp(ifa->ifa_name, "lo") == 0) {
                continue;
            }
            if (ifa->ifa_addr->sa_family == AF_INET) {
                AddressEntry info{ifa->ifa_name, getIpV4(ifa->ifa_addr), getIpV4(ifa->ifa_netmask), gate};
                const auto it = std::lower_bound(result.begin(), result.end(), info,
                                                 [](const AddressEntry& l, const AddressEntry& r) {
                                                     return l.iface < r.iface;
                                                 });
                result.emplace(it, std::move(info));
            }
        }
    }

    return result;
}

}   // namespace nhope
