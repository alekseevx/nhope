#pragma once

#include <cstddef>
#include <utility>

#include <asio/buffer.hpp>

#include "nhope/async/ao-context.h"
#include "nhope/io/io-device.h"

namespace nhope::detail {

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

    void read(gsl::span<std::uint8_t> buf, IOHandler handler) override
    {
        asioDev.async_read_some(
          asio::buffer(buf.data(), buf.size()),
          [aoCtx = AOContextRef(aoCtx), handler = std::move(handler)](auto err, auto count) mutable {
              aoCtx.exec(
                [handler = std::move(handler), err, count] {
                    handler(err, count);
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
                    handler(err, count);
                },
                Executor::ExecMode::ImmediatelyIfPossible);
          });
    }

    AsioDev asioDev;
    AOContext aoCtx;
};

}   // namespace nhope::detail
