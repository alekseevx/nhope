#include <cstddef>
#include <cstdint>
#include <exception>
#include <memory>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>

#include "nhope/io/io-device.h"
#include "nhope/async/ao-context.h"

namespace nhope {

using namespace std::literals;
IoError::IoError(std::error_code errCode, std::string_view errMessage)
  : std::system_error(errCode, errMessage.data())
{}

IoError::IoError(std::string_view errMessage)
  : std::system_error(std::make_error_code(std::errc::io_error), errMessage.data())
{}

IoEof::IoEof()
  : IoError(std::make_error_code(std::errc::io_error), "Eof"sv)
{}

namespace {

class IoReader : public std::enable_shared_from_this<IoReader>
{
public:
    explicit IoReader(IoDevice& dev, size_t bytesCount = 1)
      : m_dev(dev)
      , m_bytesCount(bytesCount)
      , m_ctx(dev.executor())
    {
        m_buffer.reserve(bytesCount);   // can throw...
    }

    nhope::AOContext& ctx() const
    {
        return m_ctx;
    }

    Future<std::vector<std::uint8_t>> read()
    {
        m_anchor = shared_from_this();
        startRead();
        return m_promise.future();
    }

    Future<std::vector<std::uint8_t>> readLine()
    {
        m_anchor = shared_from_this();
        startReadOneByOne();
        return m_promise.future();
    }

    Future<std::vector<std::uint8_t>> readAll()
    {
        m_anchor = shared_from_this();
        startReadChunks();
        return m_promise.future();
    }

private:
    void startReadOneByOne()
    {
        static constexpr unsigned char terminator = '\n';

        m_dev.read(1)
          .then(m_ctx,
                [this](const std::vector<uint8_t>& data) {
                    char s = static_cast<char>(data.at(0));
                    if (s == terminator) {
#ifdef WIN32
                        // FIXME: Терминатор должен быть строкой
                        if (!m_buffer.empty() && m_buffer.back() == '\r') {
                            m_buffer.pop_back();
                        }
#endif
                        m_promise.setValue(std::move(m_buffer));
                        m_anchor.reset();
                        return;
                    }
                    m_buffer.push_back(data.at(0));
                    startReadOneByOne();
                })
          .fail(m_ctx, [this](auto e) {
              m_promise.setException(e);
              m_anchor.reset();
          });
    }

    void startReadChunks()
    {
        constexpr size_t chunkSize = 1024;

        m_dev.read(chunkSize)
          .then(m_ctx,
                [this](const std::vector<uint8_t>& data) {
                    m_buffer.insert(m_buffer.end(), data.begin(), data.end());
                    startReadChunks();
                })
          .fail(m_ctx, [this](const std::exception_ptr& e) {
              try {
                  std::rethrow_exception(e);
              } catch (const IoEof&) {
                  m_promise.setValue(std::move(m_buffer));
              } catch (...) {
                  m_promise.setException(e);
              }
              m_anchor.reset();
          });
    }

    void startRead()
    {
        m_dev.read(m_bytesCount - m_buffer.size())
          .then(m_ctx,
                [this](const std::vector<uint8_t>& data) {
                    m_buffer.insert(m_buffer.end(), data.begin(), data.end());
                    if (m_buffer.size() >= m_bytesCount) {
                        m_buffer.resize(m_bytesCount);
                        m_promise.setValue(std::move(m_buffer));
                        m_anchor.reset();
                        return;
                    }
                    startRead();
                })
          .fail(m_ctx, [this](auto e) {
              m_promise.setException(e);
              m_anchor.reset();
          });
    }

    IoDevice& m_dev;
    const size_t m_bytesCount;
    std::vector<std::uint8_t> m_buffer;
    std::shared_ptr<IoReader> m_anchor;

    nhope::Promise<std::vector<std::uint8_t>> m_promise;

    mutable nhope::AOContext m_ctx;
};

class IoWriter : public std::enable_shared_from_this<IoWriter>
{
public:
    explicit IoWriter(IoDevice& dev, gsl::span<const std::uint8_t> data)
      : m_dev(dev)
      , m_buffer(data.begin(), data.end())
      , m_ctx(dev.executor())
    {}

    Future<size_t> write()
    {
        m_anchor = shared_from_this();
        startWrite();
        return m_promise.future();
    }

private:
    void startWrite()
    {
        auto chunk = gsl::span(m_buffer).subspan(m_offset);
        m_dev.write(chunk)
          .then(m_ctx,
                [this](const size_t count) {
                    m_offset += count;
                    if (m_offset >= m_buffer.size()) {
                        m_promise.setValue(m_buffer.size());
                        m_anchor.reset();
                        return;
                    }
                    startWrite();
                })
          .fail(m_ctx, [this](auto e) {
              m_promise.setException(e);
              m_anchor.reset();
          });
    }

    IoDevice& m_dev;
    std::shared_ptr<IoWriter> m_anchor;
    size_t m_offset{};

    std::vector<std::uint8_t> m_buffer;
    Promise<size_t> m_promise;
    nhope::AOContext m_ctx;
};

}   // namespace

Future<size_t> write(IoDevice& device, gsl::span<const std::uint8_t> data)
{
    return device.write(data);
}

Future<std::vector<std::uint8_t>> read(IoDevice& device, size_t bytesCount)
{
    return device.read(bytesCount);
}

Future<std::vector<std::uint8_t>> readExactly(IoDevice& device, size_t bytesCount)
{
    auto reader = std::make_shared<IoReader>(device, bytesCount);
    return reader->read();
}

Future<size_t> writeExactly(IoDevice& device, gsl::span<const std::uint8_t> data)
{
    auto writer = std::make_shared<IoWriter>(device, data);
    return writer->write();
}

Future<std::string> readLine(IoDevice& device)
{
    auto reader = std::make_shared<IoReader>(device);
    return reader->readLine().then(reader->ctx(), [reader](auto data) {
        return std::string(data.begin(), data.end());
    });
}

Future<std::vector<std::uint8_t>> readAll(IoDevice& device)
{
    auto reader = std::make_shared<IoReader>(device);
    return reader->readAll();
}

}   // namespace nhope
