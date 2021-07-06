#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <system_error>
#include <vector>

#include <gsl/span>

#include "nhope/async/executor.h"
#include "nhope/async/future.h"
#include "nhope/utils/noncopyable.h"

namespace nhope {

class IoError : public std::system_error
{
public:
    explicit IoError(std::error_code errCode, std::string_view errMessage = "IoError");
    explicit IoError(std::string_view errMessage);
};

class IoEof : public IoError
{
public:
    explicit IoEof();
};

class IoDevice : public Noncopyable
{
public:
    virtual ~IoDevice() = default;
    virtual Future<std::vector<std::uint8_t>> read(size_t bytesCount) = 0;
    virtual Future<size_t> write(gsl::span<const std::uint8_t> data) = 0;
    [[nodiscard]] virtual Executor& executor() const = 0;
};
using IoDevicePtr = std::unique_ptr<IoDevice>;

Future<std::vector<std::uint8_t>> read(IoDevice& device, size_t bytesCount);
Future<size_t> write(IoDevice& device, gsl::span<const std::uint8_t> data);

Future<std::vector<std::uint8_t>> readExactly(IoDevice& device, size_t bytesCount);
Future<size_t> writeExactly(IoDevice& device, gsl::span<const std::uint8_t> data);

Future<std::string> readLine(IoDevice& device);
Future<std::vector<std::uint8_t>> readAll(IoDevice& device);

}   // namespace nhope