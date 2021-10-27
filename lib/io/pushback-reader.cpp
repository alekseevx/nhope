#include <algorithm>
#include <cstdint>
#include <vector>

#include "gsl/span"

#include "nhope/async/executor.h"
#include "nhope/io/pushback-reader.h"
#include "nhope/async/ao-context.h"
#include "nhope/io/io-device.h"
#include "nhope/io/string-reader.h"

namespace nhope {

namespace {

class PushBackReaderImpl final : public PushbackReader
{
public:
    explicit PushBackReaderImpl(AOContext& parent, ReaderPtr reader)
      : m_reader(std::move(reader))
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
                handler(std::error_code(), askSize);
            });
            return;
        }

        auto next = outBuf.subspan(readyBufferSize);
        m_reader->read(next, [this, aoCtx = AOContextRef(m_aoCtx), alreadyRead = readyBufferSize,
                              handler = std::move(handler)](auto err, auto size) mutable {
            aoCtx.exec(
              [this, err, size, alreadyRead, handler = std::move(handler)] {
                  handler(err, alreadyRead + size);
              },
              Executor::ExecMode::ImmediatelyIfPossible);
        });
    }

    ReaderPtr m_reader;
    std::vector<uint8_t> m_unreadBuf;
    AOContext m_aoCtx;
};

}   // namespace

PushbackReaderPtr PushbackReader::create(AOContext& aoCtx, ReaderPtr reader)
{
    return std::make_unique<PushBackReaderImpl>(aoCtx, std::move(reader));
}

}   // namespace nhope