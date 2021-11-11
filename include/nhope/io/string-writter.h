#pragma once

#include <memory>
#include <string>

#include "nhope/async/ao-context.h"
#include "nhope/io/io-device.h"

namespace nhope {

class StringWritter;
using StringWritterPtr = std::unique_ptr<StringWritter>;

class StringWritter : public Writter
{
public:
    [[nodiscard]] virtual std::string takeContent() = 0;

    static StringWritterPtr create(AOContext& aoCtx);
};

}   // namespace nhope
