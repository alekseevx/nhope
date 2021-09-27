#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

#include <gsl/span>

#include "nhope/async/future.h"
#include "nhope/utils/noncopyable.h"

namespace nhope {

class AOContext;

class IODevice : public Noncopyable
{
public:
    using Handler = std::function<void(const std::error_code&, std::size_t)>;

    virtual ~IODevice() = default;

    virtual void read(gsl::span<std::uint8_t> buf, Handler handler) = 0;
    virtual void write(gsl::span<const std::uint8_t> data, Handler handler) = 0;
};
using IODevicePtr = std::unique_ptr<IODevice>;

Future<std::vector<std::uint8_t>> read(IODevice& dev, std::size_t bytesCount);
Future<std::size_t> write(IODevice& dev, std::vector<std::uint8_t> data);

Future<std::vector<std::uint8_t>> readExactly(IODevice& dev, std::size_t bytesCount);
Future<std::size_t> writeExactly(IODevice& dev, std::vector<std::uint8_t> data);

Future<std::string> readLine(IODevice& dev);
Future<std::vector<std::uint8_t>> readAll(IODevice& dev);

}   // namespace nhope
