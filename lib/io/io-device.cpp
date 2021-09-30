#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <memory>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include <gsl/span>
#include <gsl/span_ext>

#include "nhope/async/ao-context-error.h"
#include "nhope/async/ao-context.h"
#include "nhope/io/io-device.h"

namespace nhope {

namespace {

template<typename Handler>
class ReadOp final : public std::enable_shared_from_this<ReadOp<Handler>>
{
public:
    ReadOp(IODevice& dev, Handler&& handler)
      : m_dev(dev)
      , m_handler(std::move(handler))
    {}

    ~ReadOp()
    {
        if (!m_promise.satisfied()) {
            m_promise.setException(std::make_exception_ptr(AsyncOperationWasCancelled()));
        }
    }

    Future<std::vector<std::uint8_t>> start()
    {
        this->readNextPortion();
        return m_promise.future();
    }

private:
    using std::enable_shared_from_this<ReadOp>::shared_from_this;

    void readNextPortion()
    {
        m_portionSize = m_handler(m_buf);
        if (m_portionSize == 0) {
            m_promise.setValue(std::move(m_buf));
            return;
        }

        m_buf.resize(m_buf.size() + m_portionSize);
        auto bufForNextPortion = gsl::span(m_buf).last(m_portionSize);
        m_dev.read(bufForNextPortion, [self = shared_from_this()](auto err, auto count) {
            self->readPortionHandler(err, count);
        });
    }

    void readPortionHandler(const std::error_code& err, std::size_t count)
    {
        assert(m_portionSize >= count);   // NOLINT

        if (err) {
            m_promise.setException(std::make_exception_ptr(std::system_error(err)));
            return;
        }

        m_buf.resize(m_buf.size() - (m_portionSize - count));
        if (count == 0) {
            // EOF
            m_promise.setValue(std::move(m_buf));
            return;
        }

        this->readNextPortion();
    }

    IODevice& m_dev;
    Promise<std::vector<std::uint8_t>> m_promise;
    std::vector<std::uint8_t> m_buf;
    std::size_t m_portionSize = 0;
    Handler m_handler;
};

template<typename Handler>
std::shared_ptr<ReadOp<Handler>> makeReadOp(IODevice& dev, Handler&& handler)
{
    return std::make_shared<ReadOp<Handler>>(dev, std::move(handler));
}

class WriteOp final : public std::enable_shared_from_this<WriteOp>
{
public:
    WriteOp(IODevice& dev, std::vector<std::uint8_t> data, bool writeAll)
      : m_dev(dev)
      , m_data(std::move(data))
      , m_writeAll(writeAll)
    {}

    ~WriteOp()
    {
        if (!m_promise.satisfied()) {
            m_promise.setException(std::make_exception_ptr(AsyncOperationWasCancelled()));
        }
    }

    Future<std::size_t> start()
    {
        this->writeNextPortion();
        return m_promise.future();
    }

private:
    void writeNextPortion()
    {
        const auto portion = gsl::span(m_data).subspan(m_written);
        m_dev.write(portion, [self = shared_from_this()](auto err, auto count) {
            self->writePortionHandler(err, count);
        });
    }

    void writePortionHandler(const std::error_code& err, std::size_t count)
    {
        if (err) {
            m_promise.setException(std::make_exception_ptr(std::system_error(err)));
            return;
        }

        m_written += count;
        if (m_written < m_data.size() && m_writeAll) {
            writeNextPortion();
            return;
        }

        m_promise.setValue(static_cast<std::size_t>(m_written));
    }

    IODevice& m_dev;
    Promise<std::size_t> m_promise;
    const std::vector<std::uint8_t> m_data;
    const bool m_writeAll;
    std::size_t m_written = 0;
};

#ifdef WIN32
constexpr std::array<std::uint8_t, 2> endLineMarker = {static_cast<std::uint8_t>('\r'),
                                                       static_cast<std::uint8_t>('\n')};
#else
constexpr std::array<std::uint8_t, 1> endLineMarker = {static_cast<std::uint8_t>('\n')};
#endif

bool hasEndLineMarker(gsl::span<const std::uint8_t> data)
{
    if (data.size() < endLineMarker.size()) {
        return false;
    }

    return data.last(endLineMarker.size()) == gsl::span(endLineMarker);
}

}   // namespace

Future<std::vector<std::uint8_t>> read(IODevice& dev, std::size_t bytesCount)
{
    auto readOp = makeReadOp(dev, [bytesCount](auto& /*unused*/) mutable {
        return std::exchange(bytesCount, 0);
    });

    return readOp->start();
}

Future<std::size_t> write(IODevice& dev, std::vector<std::uint8_t> data)
{
    auto writeOp = std::make_shared<WriteOp>(dev, std::move(data), false);
    return writeOp->start();
}

Future<std::vector<std::uint8_t>> readExactly(IODevice& dev, size_t bytesCount)
{
    auto readOp = makeReadOp(dev, [bytesCount](const auto& buf) {
        assert(bytesCount >= buf.size());   // NOLINT
        const std::size_t nextPortionSize = bytesCount - buf.size();
        return nextPortionSize;
    });

    return readOp->start();
}

Future<size_t> writeExactly(IODevice& device, std::vector<std::uint8_t> data)
{
    auto writeOp = std::make_shared<WriteOp>(device, std::move(data), true);
    return writeOp->start();
}

Future<std::string> readLine(IODevice& dev)
{
    auto readOp = makeReadOp(dev, [](const auto& buf) {
        const std::size_t nextPortionSize = hasEndLineMarker(buf) ? 0 : 1;
        return nextPortionSize;
    });

    return readOp->start().then([](const auto& buf) {
        std::size_t lineSize = buf.size();
        if (hasEndLineMarker(buf)) {
            lineSize -= endLineMarker.size();
        }

        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
        return std::string(reinterpret_cast<const char*>(buf.data()), lineSize);
    });
}

Future<std::vector<std::uint8_t>> readAll(IODevice& dev)
{
    auto readOp = makeReadOp(dev, [](const auto& /*unused*/) {
        constexpr size_t portionSize = 4 * 1024;
        return portionSize;
    });

    return readOp->start();
}

}   // namespace nhope
