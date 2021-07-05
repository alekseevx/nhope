#pragma once

#include <cstdint>
#include <string>

namespace nhope {

struct Endpoint
{
    uint16_t port;
    std::string host{"0.0.0.0"};
};

}   // namespace nhope