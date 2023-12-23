#pragma once

#include <exception>
#include <system_error>
#include <utility>

#include <asio/buffer.hpp>

#include "asio/error.hpp"
#include "nhope/async/ao-context.h"
#include "nhope/io/io-device.h"

namespace nhope::detail {

inline std::exception_ptr toExceptionPtr(std::error_code errCode)
{
    using asio::error::misc_errors;
    using asio::error::misc_category;

    if (!errCode) {
        return nullptr;
    }
    if (misc_category.equivalent(errCode, misc_errors::eof)) {
        return nullptr;
    }

    return std::make_exception_ptr(std::system_error(errCode));
}

template<typename BaseClass, typename AsioDev>
class AsioDeviceWrapper : public BaseClass
{
public:
    explicit AsioDeviceWrapper(AOContextRef& parentAOCtx)
      : asioDev(parentAOCtx.executor().ioCtx())
      , aoCtx(parentAOCtx)
    {}

    explicit AsioDeviceWrapper(AOContext& parentAOCtx)
      : asioDev(parentAOCtx.executor().ioCtx())
      , aoCtx(parentAOCtx)
    {}

    ~AsioDeviceWrapper() override
    {
        aoCtx.close();
    }

    void ioCancel() override
    {
        asioDev.cancel();
    }

    void read(gsl::span<std::uint8_t> buf, IOHandler handler) override
    {
        asioDev.async_read_some(
          asio::buffer(buf.data(), buf.size()),
          [aoCtx = AOContextRef(aoCtx), handler = std::move(handler)](auto err, auto count) mutable {
              aoCtx.exec(
                [handler = std::move(handler), err, count] {
                    handler(toExceptionPtr(err), count);
                },
                Executor::ExecMode::ImmediatelyIfPossible);
          });
    }

    void write(gsl::span<const std::uint8_t> data, IOHandler handler) override
    {
        asioDev.async_write_some(
          asio::buffer(data.data(), data.size()),
          [aoCtx = AOContextRef(aoCtx), handler = std::move(handler)](auto& err, auto count) mutable {
              aoCtx.exec(
                [handler = std::move(handler), err, count] {
                    handler(toExceptionPtr(err), count);
                },
                Executor::ExecMode::ImmediatelyIfPossible);
          });
    }

    AsioDev asioDev;
    AOContext aoCtx;
};

}   // namespace nhope::detail
