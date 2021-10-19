#pragma once

#include <memory>
#include <string>

#include "nhope/async/ao-context.h"
#include "nhope/io/io-device.h"

namespace nhope {

class StringReader;
using StringReaderPtr = std::unique_ptr<StringReader>;

class StringReader : public Reader
{
public:
    static StringReaderPtr create(AOContext& aoCtx, std::string str);
};

}   // namespace nhope
