#pragma once

#include <chrono>
#include <functional>
#include <memory>
#include <system_error>

#include <boost/asio/io_context.hpp>

namespace nhope::asyncs {

class Timer
{
public:
    using Handler = std::function<void(const std::error_code&)>;

public:
    virtual ~Timer() = default;

    virtual bool isExpired() const = 0;

public:
    static std::unique_ptr<Timer> start(boost::asio::io_context& ctx, const std::chrono::nanoseconds& expiryTime,
                                        Handler&& handler);

protected:
    Timer() = default;
    Timer(const Timer&) = delete;
    Timer& operator=(const Timer&) = delete;
};

}   // namespace nhope::asyncs
