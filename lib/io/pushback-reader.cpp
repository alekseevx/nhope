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
        if (!m_unreadBuf.empty()) {
            const auto size = std::min(outBuf.size(), m_unreadBuf.size());
            const auto bufSpan = gsl::span(m_unreadBuf).last(size);

            std::copy(bufSpan.rbegin(), bufSpan.rend(), outBuf.begin());
            m_unreadBuf.resize(m_unreadBuf.size() - size);

            m_aoCtx.exec([size, handler = std::move(handler)] {
                handler(nullptr, size);
            });
            return;
        }

        m_originReader.read(
          outBuf, [this, aoCtx = AOContextRef(m_aoCtx), handler = std::move(handler)](auto err, auto size) mutable {
              aoCtx.exec(
                [this, err = std::move(err), size, handler = std::move(handler)] {
                    handler(std::move(err), size);
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
