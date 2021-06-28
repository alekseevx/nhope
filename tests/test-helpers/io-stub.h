#pragma once

#include <chrono>
#include <cstddef>
#include <functional>
#include <stdexcept>
#include <thread>
#include <utility>

#include "asio/buffer.hpp"
#include "asio/error_code.hpp"
#include "asio/io_context.hpp"
#include "asio/post.hpp"

#include "nhope/async/async-invoke.h"
#include "nhope/async/thread-executor.h"
#include "nhope/io/io-device.h"
#include "nhope/io/detail/asio-device.h"

namespace nhope {

static inline constexpr auto badSize{42};

class AsioStub
{
public:
    explicit AsioStub(asio::io_context& ctx)
      : ioEx(ctx)
    {}

    void open(asio::error_code& /*unused*/)
    {
        m_isOpen = true;
    }

    [[nodiscard]] bool is_open() const
    {
        return m_isOpen;
    }

    template<typename Buff, typename Handler>
    void async_read_some(const Buff& buffers, Handler&& handler)
    {
        // asio::post(ioEx, [&] {
        asio::error_code ec;
        if (buffers.size() == badSize) {
            ec.assign(1, asio::system_category());
        }
        handler(ec, buffers.size());
        // });
    }

    template<typename Buff, typename Handler>
    void async_write_some(const Buff& buffers, Handler&& handler)
    {
        asio::error_code ec;
        if (buffers.size() == badSize) {
            ec.assign(1, asio::system_category());
        }
        handler(ec, buffers.size());

        // asio::post(ioEx, [&] {

        // });
    }

    void close()
    {
        m_isOpen = false;
    }

private:
    asio::io_context& ioEx;
    bool m_isOpen{};
};
using AsioStubDev = detail::AsioDevice<AsioStub>;

class StubDevice : public IoDevice
{
public:
    StubDevice()
      : m_ctx(m_thread)
    {}

    void open()
    {
        nhope::invoke(m_ctx, [this] {
            m_closed = false;
        });
    }

    void close()
    {
        nhope::invoke(m_ctx, [this] {
            m_closed = true;
        });
    }

    Future<std::vector<std::uint8_t>> read(size_t bytesCount) override
    {
        return asyncInvoke(m_ctx, [this, bytesCount] {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));

            if (bytesCount == badSize || m_closed) {
                throw IoError("bad");
            }
            std::vector<std::uint8_t> data(3);
            return data;
        });
    }

    Future<size_t> write(gsl::span<const std::uint8_t> data) override
    {
        return asyncInvoke(m_ctx, [this, data] {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            if (data.size() == badSize || m_closed) {
                throw IoError("bad");
            }
            return size_t(3);
        });
    }

    [[nodiscard]] Executor& executor() const override
    {
        return m_thread;
    }

private:
    mutable nhope::ThreadExecutor m_thread;
    bool m_closed{};
    nhope::AOContext m_ctx;
};

}   // namespace nhope