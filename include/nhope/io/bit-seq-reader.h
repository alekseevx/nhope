#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include "gsl/span"
#include "nhope/io/io-device.h"

namespace nhope {

class BitSeqReader;
using BitSeqReaderPtr = std::unique_ptr<BitSeqReader>;

class BitSeqReader : public Reader
{
public:
    static BitSeqReaderPtr create(AOContext& aoCtx, std::vector<bool> bits);
    static BitSeqReaderPtr create(AOContext& aoCtx, gsl::span<const uint8_t> psp, size_t bitCount);
};

}   // namespace nhope
