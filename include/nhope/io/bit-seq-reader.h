#pragma once

#include <memory>
#include <vector>

#include "nhope/io/io-device.h"

namespace nhope {

class BitSeqReader;
using BitSeqReaderPtr = std::unique_ptr<BitSeqReader>;

class BitSeqReader : public Reader
{
public:
    static BitSeqReaderPtr create(AOContext& aoCtx, std::vector<bool> bits);
};

}   // namespace nhope
