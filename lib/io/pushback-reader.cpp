#include <algorithm>
#include <cstdint>
#include <vector>

#include <gsl/span>

#include "nhope/async/ao-context.h"
#include "nhope/async/executor.h"
#include "nhope/io/io-device.h"
#include "nhope/io/pushback-reader.h"

namespace nhope {

namespace {

class PushBackReaderImpl final : public PushbackReader
{
public:
    explicit PushBackReaderImpl(AOContext& parent, Reader& reader)
      : m_originReader(reader)
      , m_aoCtx(parent)
    {}

    ~PushBackReaderImpl() final
    {
        m_aoCtx.close();
    }

    void read(gsl::span<std::uint8_t> buf, IOHandler handler) final
    {
        this->startRead(buf, std::move(handler));
    }

    void unread(gsl::span<const std::uint8_t> bytes) final
    {
        m_unreadBuf.insert(m_unreadBuf.end(), bytes.rbegin(), bytes.rend());
    }

private:
    void startRead(gsl::span<std::uint8_t> outBuf, IOHandler handler)
    {
        const auto askSize = outBuf.size();
        const auto currentBufferSize = m_unreadBuf.size();

        const auto readyBufferSize = std::min(currentBufferSize, askSize);
        if (readyBufferSize != 0) {
            const auto bufSpan = gsl::span(m_unreadBuf).last(readyBufferSize);
            std::copy(bufSpan.rbegin(), bufSpan.rend(), outBuf.begin());
            m_unreadBuf.resize(currentBufferSize - readyBufferSize);
        }

        const auto leftReadChunk = askSize - readyBufferSize;
        if (leftReadChunk == 0) {
            m_aoCtx.exec([askSize, handler = std::move(handler)] {
                handler(nullptr, askSize);
            });
            return;
        }

        auto next = outBuf.subspan(readyBufferSize);
        m_originReader.read(next, [this, aoCtx = AOContextRef(m_aoCtx), alreadyRead = readyBufferSize,
                                   handler = std::move(handler)](auto err, auto size) mutable {
            aoCtx.exec(
              [this, err = std::move(err), size, alreadyRead, handler = std::move(handler)] {
                  handler(std::move(err), alreadyRead + size);
              },
              Executor::ExecMode::ImmediatelyIfPossible);
        });
    }

    Reader& m_originReader;
    std::vector<uint8_t> m_unreadBuf;
    AOContext m_aoCtx;
};

class PushBackReaderOwnerImpl final : public PushbackReader
{
public:
    PushBackReaderOwnerImpl(AOContext& parent, ReaderPtr reader)
      : m_originReader(std::move(reader))
      , m_pushbackReader(parent, *m_originReader)
    {}

    void read(gsl::span<std::uint8_t> buf, IOHandler handler) final
    {
        m_pushbackReader.read(buf, std::move(handler));
    }

    void unread(gsl::span<const std::uint8_t> bytes) final
    {
        m_pushbackReader.unread(bytes);
    }

private:
    ReaderPtr m_originReader;
    PushBackReaderImpl m_pushbackReader;
};

}   // namespace

PushbackReaderPtr PushbackReader::create(AOContext& aoCtx, Reader& reader)
{
    return std::make_unique<PushBackReaderImpl>(aoCtx, reader);
}

PushbackReaderPtr PushbackReader::create(AOContext& aoCtx, ReaderPtr reader)
{
    return std::make_unique<PushBackReaderOwnerImpl>(aoCtx, std::move(reader));
}

}   // namespace nhope
