#include <string>
#include <vector>
#include <array>
#include <cassert>

#include <WinSock2.h>
#include <ws2ipdef.h>
#include <iphlpapi.h>
#include <WS2tcpip.h>

#include "nhope/io/network.h"
#include "nhope/utils/scope-exit.h"

#include <fmt/format.h>
#pragma

#pragma comment(lib, "IPHLPAPI.lib")
#pragma comment(lib, "Ws2_32.lib")

#define MALLOC(x) HeapAlloc(GetProcessHeap(), 0, (x))
#define FREE(x) HeapFree(GetProcessHeap(), 0, (x))

namespace nhope {

namespace {

std::string from(IN_ADDR adr)
{
    char str_buffer[INET_ADDRSTRLEN + 1] = {0};
    inet_ntop(AF_INET, &adr, str_buffer, INET_ADDRSTRLEN);
    return std::string(str_buffer);
}

std::string ifaceName(PIP_ADAPTER_INFO ptr)
{
    NET_LUID luid{};
    ConvertInterfaceIndexToLuid(ptr->Index, &luid);
    char buf[IF_MAX_STRING_SIZE + 1]{};
    if (ConvertInterfaceLuidToNameA(&luid, buf, IF_MAX_STRING_SIZE) == NO_ERROR) {
        return buf;
    }
    return ptr->AdapterName;
}

}   // namespace

std::string getLocalIp()
{
    WSADATA wsaData;
    constexpr auto wVersionRequested = MAKEWORD(2, 2);
    WSAStartup(wVersionRequested, &wsaData);
    constexpr auto maxHost{256};
    char hostname[maxHost] = {0};

#pragma warning(push)
#pragma warning(disable : 4996)
    gethostname(hostname, maxHost - 1);
    struct hostent* ent = gethostbyname(hostname);
    // getaddrinfo()
#pragma warning(pop)
    struct in_addr ip_addr = *(struct in_addr*)(ent->h_addr);

    WSACleanup();
    return from(ip_addr);
}

std::vector<AddressEntry> addressEntries()
{
    std::vector<AddressEntry> result;
    PIP_ADAPTER_INFO pAdapterInfo;
    PIP_ADAPTER_INFO pAdapter = nullptr;
    ULONG ulOutBufLen = sizeof(IP_ADAPTER_INFO);
    pAdapterInfo = (IP_ADAPTER_INFO*)MALLOC(sizeof(IP_ADAPTER_INFO));
    nhope::ScopeExit cleaner([&pAdapterInfo] {
        if (pAdapterInfo) {
            FREE(pAdapterInfo);
        }
    });
    assert(pAdapterInfo != nullptr);

    if (GetAdaptersInfo(pAdapterInfo, &ulOutBufLen) == ERROR_BUFFER_OVERFLOW) {
        FREE(pAdapterInfo);
        pAdapterInfo = (IP_ADAPTER_INFO*)MALLOC(ulOutBufLen);
        assert(pAdapterInfo != nullptr);
    }

    if (GetAdaptersInfo(pAdapterInfo, &ulOutBufLen) == NO_ERROR) {
        pAdapter = pAdapterInfo;
        while (pAdapter) {
            AddressEntry entry{};
            entry.ip = pAdapter->IpAddressList.IpAddress.String;
            entry.mask = pAdapter->IpAddressList.IpMask.String;
            entry.gateway = pAdapter->GatewayList.IpAddress.String;

            entry.iface = ifaceName(pAdapter);
            const auto it =
              std::lower_bound(result.begin(), result.end(), entry, [](const AddressEntry& l, const AddressEntry& r) {
                  return l.iface < r.iface;
              });
            result.emplace(it, std::move(entry));

            pAdapter = pAdapter->Next;
        }
    }

    return result;
}

}   // namespace nhope