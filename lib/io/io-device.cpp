#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <list>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "gsl/span"
#include "gsl/span_ext"

#include "nhope/async/ao-context-error.h"
#include "nhope/async/ao-context.h"
#include "nhope/async/executor.h"
#include "nhope/async/future.h"
#include "nhope/io/io-device.h"
#include "nhope/utils/detail/ref-ptr.h"

namespace nhope {

namespace {

template<typename Handler>
class ReadOp final : public detail::BaseRefCounter
{
public:
    ReadOp(Reader& dev, Handler&& handler)
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
    void readNextPortion()
    {
        using detail::refPtrFromRawPtr;

        m_portionSize = m_handler(m_buf);
        if (m_portionSize == 0) {
            m_promise.setValue(std::move(m_buf));
            return;
        }

        m_buf.resize(m_buf.size() + m_portionSize);
        auto bufForNextPortion = gsl::span(m_buf).last(m_portionSize);
        m_dev.read(bufForNextPortion, [self = refPtrFromRawPtr(this)](auto err, auto count) {
            self->readPortionHandler(std::move(err), count);
        });
    }

    void readPortionHandler(std::exception_ptr err, std::size_t count)
    {
        assert(m_portionSize >= count);   // NOLINT

        if (err) {
            m_promise.setException(std::move(err));
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

    Reader& m_dev;   // NOLINT cppcoreguidelines-avoid-const-or-ref-data-members
    Promise<std::vector<std::uint8_t>> m_promise;
    std::vector<std::uint8_t> m_buf;
    std::size_t m_portionSize = 0;
    Handler m_handler;
};

template<typename Handler>
detail::RefPtr<ReadOp<Handler>> makeReadOp(Reader& dev, Handler&& handler)
{
    return detail::makeRefPtr<ReadOp<Handler>>(dev, std::forward<Handler>(handler));
}

class WriteOp final : public detail::BaseRefCounter
{
public:
    WriteOp(Writter& dev, std::vector<std::uint8_t> data, bool writeAll)
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
        using detail::refPtrFromRawPtr;

        const auto portion = gsl::span(m_data).subspan(m_written);
        m_dev.write(portion, [self = refPtrFromRawPtr(this)](auto err, auto count) {
            self->writePortionHandler(err, count);
        });
    }

    void writePortionHandler(std::exception_ptr err, std::size_t count)
    {
        if (err) {
            m_promise.setException(std::move(err));
            return;
        }

        m_written += count;
        if (m_written < m_data.size() && m_writeAll) {
            writeNextPortion();
            return;
        }

        m_promise.setValue(static_cast<std::size_t>(m_written));
    }

    Writter& m_dev;   // NOLINT cppcoreguidelines-avoid-const-or-ref-data-members
    Promise<std::size_t> m_promise;
    const std::vector<std::uint8_t> m_data;
    const bool m_writeAll;
    std::size_t m_written = 0;
};

class CopyOp final : public detail::BaseRefCounter
{
public:
    CopyOp(nhope::Reader& src, nhope::Writter& dest)
      : m_src(src)
      , m_dest(dest)
      , m_buf(bufSize)
    {}

    ~CopyOp()
    {
        if (!m_promise.satisfied()) {
            m_promise.setException(std::make_exception_ptr(AsyncOperationWasCancelled()));
        }
    }

    Future<std::size_t> start()
    {
        this->readNextPortion();
        return m_promise.future();
    }

private:
    void readNextPortion()
    {
        using detail::refPtrFromRawPtr;

        m_src.read(m_buf, [self = refPtrFromRawPtr(this)](auto err, auto count) {
            if (err) {
                self->m_promise.setException(std::move(err));
                return;
            }

            if (count == 0) {
                // The src return EOF
                self->m_promise.setValue(self->m_byteCounter);
                return;
            }

            self->writePortion(0, count);
        });
    }

    void writePortion(std::size_t offset, std::size_t size)
    {
        using detail::refPtrFromRawPtr;

        assert(offset + size <= m_buf.size());   // NOLINT

        auto portion = gsl::span(m_buf).subspan(offset, size);
        m_dest.write(portion, [offset, size, self = refPtrFromRawPtr(this)](auto err, auto count) {
            assert(count <= size);   // NOLINT

            if (err) {
                self->m_promise.setException(std::move(err));
                return;
            }

            self->m_byteCounter += count;

            if (size == count) {
                self->readNextPortion();
                return;
            }

            self->writePortion(offset + count, size - count);
        });
    }

    static constexpr std::size_t bufSize = 4 * 1024;

    Promise<std::size_t> m_promise;
    nhope::Reader& m_src;
    nhope::Writter& m_dest;
    std::vector<std::uint8_t> m_buf;

    std::size_t m_byteCounter = 0;
};

class ConcatReader final : public Reader
{
public:
    ConcatReader(AOContext& parent, std::list<ReaderPtr> readers)
      : m_aoCtx(parent)
      , m_readers(std::move(readers))
    {}

    ~ConcatReader() override
    {
        m_aoCtx.close();
    }

    void read(gsl::span<std::uint8_t> buf, IOHandler handler) override
    {
        this->startRead(buf, std::move(handler));
    }

private:
    void startRead(gsl::span<std::uint8_t> buf, IOHandler handler)
    {
        m_aoCtx.exec([this, buf, handler = std::move(handler)]() mutable {
            if (m_readers.empty()) {
                handler(nullptr, 0);
                return;
            }

            auto& currReader = m_readers.front();
            currReader->read(buf, [this, aoCtx = AOContextRef(m_aoCtx), buf,
                                   handler = std::move(handler)](auto err, auto size) mutable {
                aoCtx.exec(
                  [this, buf, err, size, handler = std::move(handler)]() mutable {
                      if (err) {
                          handler(err, size);
                          return;
                      }

                      if (size == 0) {
                          m_readers.pop_front();
                          this->startRead(buf, std::move(handler));
                          return;
                      }

                      handler(err, size);
                  },
                  Executor::ExecMode::ImmediatelyIfPossible);
            });
        });
    }

    AOContext m_aoCtx;
    std::list<ReaderPtr> m_readers;
};

bool endsWith(gsl::span<const std::uint8_t> data, gsl::span<const std::uint8_t> expect)
{
    const auto expectSize = expect.size();
    if (data.size() < expectSize) {
        return false;
    }
    return data.last(expectSize) == expect;
}

#ifdef WIN32
constexpr std::array<std::uint8_t, 2> endLineMarker = {static_cast<std::uint8_t>('\r'),
                                                       static_cast<std::uint8_t>('\n')};
#else
constexpr std::array<std::uint8_t, 1> endLineMarker = {static_cast<std::uint8_t>('\n')};
#endif

bool hasEndLineMarker(gsl::span<const std::uint8_t> data)
{
    return endsWith(data, endLineMarker);
}

}   // namespace

Future<std::vector<std::uint8_t>> read(Reader& dev, std::size_t bytesCount)
{
    auto readOp = makeReadOp(dev, [bytesCount](auto& /*unused*/) mutable {
        return std::exchange(bytesCount, 0);
    });

    return readOp->start();
}

Future<std::size_t> write(Writter& dev, std::vector<std::uint8_t> data)
{
    auto writeOp = detail::makeRefPtr<WriteOp>(dev, std::move(data), false);
    return writeOp->start();
}

Future<std::vector<std::uint8_t>> readExactly(Reader& dev, std::size_t bytesCount)
{
    auto readOp = makeReadOp(dev, [bytesCount](const auto& buf) {
        assert(bytesCount >= buf.size());   // NOLINT
        const std::size_t nextPortionSize = bytesCount - buf.size();
        return nextPortionSize;
    });

    return readOp->start();
}

Future<std::size_t> writeExactly(Writter& device, std::vector<std::uint8_t> data)
{
    auto writeOp = detail::makeRefPtr<WriteOp>(device, std::move(data), true);
    return writeOp->start();
}

Future<std::vector<std::uint8_t>> readUntil(Reader& dev, std::vector<std::uint8_t> expect)
{
    auto readOp = makeReadOp(dev, [expect = std::move(expect)](const auto& buf) {
        const std::size_t nextPortionSize = endsWith(buf, expect) ? 0 : 1;
        return nextPortionSize;
    });

    return readOp->start();
}

Future<std::string> readLine(Reader& dev)
{
    return readUntil(dev, std::vector<std::uint8_t>(endLineMarker.begin(), endLineMarker.end()))
      .then([](const auto& buf) {
          std::size_t lineSize = buf.size();
          if (hasEndLineMarker(buf)) {
              lineSize -= endLineMarker.size();
          }

          // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
          return std::string(reinterpret_cast<const char*>(buf.data()), lineSize);
      });
}

Future<std::vector<std::uint8_t>> readAll(Reader& dev)
{
    auto readOp = makeReadOp(dev, [](const auto& /*unused*/) {
        constexpr std::size_t portionSize = 4 * 1024;
        return portionSize;
    });

    return readOp->start();
}

Future<std::vector<std::uint8_t>> readAll(ReaderPtr dev)
{
    return readAll(*dev).then([anchor = std::move(dev)](auto&& data) {
        return std::move(data);
    });
}

Future<std::size_t> copy(nhope::Reader& src, nhope::Writter& dest)
{
    auto copyOp = detail::makeRefPtr<CopyOp>(src, dest);
    return copyOp->start();
}

Future<std::size_t> copy(ReaderPtr src, WritterPtr dest)
{
    return copy(*src, *dest).then([srcAnchor = std::move(src), destAnchor = std::move(dest)](auto n) {
        return n;
    });
}

ReaderPtr concat(AOContext& aoCtx, std::list<ReaderPtr> readers)
{
    return std::make_unique<ConcatReader>(aoCtx, std::move(readers));
}

}   // namespace nhope
