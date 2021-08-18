#pragma once

#include <cstdint>
#include <string>

namespace nhope {

struct Endpoint
{
    std::uint16_t port = 0;
    std::string host{"0.0.0.0"};
};

}   // namespace nhope