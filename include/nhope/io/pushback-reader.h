#pragma once

#include <cstdint>
#include <memory>

#include <gsl/span>

#include "nhope/async/ao-context.h"
#include "nhope/io/io-device.h"

namespace nhope {

class PushbackReader;
using PushbackReaderPtr = std::unique_ptr<PushbackReader>;

class PushbackReader : public Reader
{
public:
    virtual void unread(gsl::span<const std::uint8_t> bytes) = 0;

    static PushbackReaderPtr create(AOContext& aoCtx, Reader& reader);
    static PushbackReaderPtr create(AOContext& aoCtx, ReaderPtr reader);
};

}   // namespace nhope
