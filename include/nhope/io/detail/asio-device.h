#pragma once

#include <memory>

#include "asio/buffer.hpp"
#include "asio/error.hpp"

#include "nhope/io/io-device.h"
#include "nhope/async/async-invoke.h"
#include "nhope/async/future.h"
#include "nhope/async/ao-context.h"
#include "nhope/async/executor.h"

namespace nhope::detail {

template<typename AsioType>
class AsioDevice final : public IoDevice
{
public:
    explicit AsioDevice(nhope::Executor& executor)
      : m_impl(executor.ioCtx())
      , m_ctx(executor)
    {}

    nhope::Future<std::vector<std::uint8_t>> read(size_t bytesCount) final
    {
        auto buf = std::vector<std::uint8_t>(bytesCount);
        nhope::Promise<std::vector<std::uint8_t>> promise;
        auto future = promise.future();

        auto asioBuf = asio::buffer(buf.data(), buf.size());
        m_impl.async_read_some(asioBuf,
                               [b = std::move(buf), p = std::move(promise)](const auto& err, size_t count) mutable {
                                   if (err) {
                                       p.setException(std::make_exception_ptr(IoError(err.message())));
                                       return;
                                   }

                                   b.resize(count);
                                   p.setValue(b);
                               });
        return future;
    }

    nhope::Future<size_t> write(gsl::span<const std::uint8_t> data) final
    {
        auto buf = std::vector<std::uint8_t>(data.begin(), data.end());
        nhope::Promise<size_t> promise;
        auto future = promise.future();

        auto asioBuf = asio::buffer(buf.data(), buf.size());
        m_impl.async_write_some(asioBuf,
                                [b = std::move(buf), p = std::move(promise)](const auto& err, size_t count) mutable {
                                    if (err) {
                                        p.setException(std::make_exception_ptr(IoError(err.message())));
                                        return;
                                    }

                                    p.setValue(count);
                                });
        return future;
    }

    [[nodiscard]] nhope::Executor& executor() const final
    {
        return m_ctx.executor();
    }

    AsioType& impl()
    {
        return m_impl;
    }

private:
    AsioType m_impl;
    std::vector<uint8_t> m_sendBuffer;
    nhope::Future<size_t> m_sendDataFuture;

    mutable nhope::AOContext m_ctx;
};

}   // namespace nhope::detail
