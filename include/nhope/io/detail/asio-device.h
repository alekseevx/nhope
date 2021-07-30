#pragma once

#include <memory>
#include <utility>

#include <asio/buffer.hpp>
#include <asio/error.hpp>
#include <asio/io_context.hpp>

#include "nhope/io/io-device.h"
#include "nhope/async/future.h"
#include "nhope/async/ao-context.h"
#include "nhope/async/executor.h"

namespace nhope::detail {

template<typename AsioType>
class AsioDevice final : public IoDevice
{
public:
    explicit AsioDevice(Executor& executor)
      : m_impl(executor.ioCtx())
      , m_executor(executor)
    {}

    nhope::Future<std::vector<std::uint8_t>> read(std::size_t bytesCount) final
    {
        nhope::Promise<std::vector<std::uint8_t>> promise;
        auto future = promise.future();

        std::vector<std::uint8_t> buf(bytesCount);
        auto asioBuf = asio::buffer(buf);
        m_impl.async_read_some(asioBuf, [b = std::move(buf), p = std::move(promise)](auto& err, auto count) mutable {
            if (err) {
                p.setException(std::make_exception_ptr(IoError(err)));
                return;
            }
            b.resize(count);
            p.setValue(std::move(b));
        });

        return future;
    }

    nhope::Future<size_t> write(gsl::span<const std::uint8_t> data) final
    {
        auto buf = std::vector<std::uint8_t>(data.begin(), data.end());
        nhope::Promise<size_t> promise;
        auto future = promise.future();

        auto asioBuf = asio::buffer(buf.data(), buf.size());
        m_impl.async_write_some(asioBuf, [b = std::move(buf), p = std::move(promise)](auto& err, auto count) mutable {
            if (err) {
                p.setException(std::make_exception_ptr(IoError(err)));
                return;
            }

            p.setValue(count);
        });
        return future;
    }

    [[nodiscard]] Executor& executor() const final
    {
        return m_executor;
    }

    AsioType& impl() noexcept
    {
        return m_impl;
    }

private:
    AsioType m_impl;
    Executor& m_executor;
};

}   // namespace nhope::detail
