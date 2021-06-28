#pragma once

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

    ~AsioDevice() override
    {
        if (m_impl.is_open()) {
            m_impl.close();
        }
    }

    nhope::Future<std::vector<std::uint8_t>> read(size_t bytesCount) final
    {
        return nhope::asyncInvoke(m_ctx, [this, bytesCount] {
            auto result = std::make_shared<std::vector<std::uint8_t>>(bytesCount);
            nhope::Promise<std::vector<std::uint8_t>> promise;
            auto r = promise.future();

            m_impl.async_read_some(asio::buffer(*result),
                                   [result, p = std::move(promise)](const auto& err, size_t count) mutable {
                                       if (err) {
                                           p.setException(std::make_exception_ptr(IoError(err.message())));
                                           return;
                                       }
                                       result->resize(count);
                                       p.setValue(*result);
                                   });
            return r;
        });
    }

    nhope::Future<size_t> write(gsl::span<const std::uint8_t> data) final
    {
        return nhope::asyncInvoke(m_ctx, [this, data] {
            auto send = std::make_shared<std::vector<std::uint8_t>>(data.begin(), data.end());
            nhope::Promise<size_t> promise;
            auto r = promise.future();
            m_impl.async_write_some(asio::buffer(*send),
                                    [p = std::move(promise)](const auto& err, size_t count) mutable {
                                        if (err) {
                                            p.setException(std::make_exception_ptr(IoError(err.message())));
                                            return;
                                        }
                                        p.setValue(count);
                                    });
            return r;
        });
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
