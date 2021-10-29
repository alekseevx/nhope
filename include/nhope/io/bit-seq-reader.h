#pragma once

#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include "nhope/io/io-device.h"

namespace nhope {

class BitSeqReader;
using BitSeqReaderPtr = std::unique_ptr<BitSeqReader>;

class BitSeqReader : public Reader
{
public:
    static BitSeqReaderPtr create(AOContext& aoCtx, std::vector<bool> bits);
    static BitSeqReaderPtr create(AOContext& aoCtx, std::span<const uint8_t> psp, std::size_t bitCount);
};

}   // namespace nhope
