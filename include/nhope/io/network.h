#pragma once

#include <string>
#include <vector>

namespace nhope {

std::string getLocalIp();

struct AddressEntry
{
    std::string iface;
    std::string ip;
    std::string mask;
    std::string gateway;

    bool operator==(const AddressEntry& o) const noexcept
    {
        return iface == o.iface && ip == o.ip && mask == o.mask && gateway == o.gateway;
    }
    bool operator!=(const AddressEntry& o) const noexcept
    {
        return !(*this == o);
    }
};

std::vector<AddressEntry> addressEntries();

}   // namespace nhope