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
    explicit PushBackReaderImpl(AOContext& aoCtx, ReaderPtr reader)
      : m_reader(std::move(reader))
      , m_ctx(aoCtx)
    {}

    void read(gsl::span<std::uint8_t> buf, IOHandler handler) final
    {
        m_ctx.exec([this, buf, handler = std::move(handler)]() mutable {
            startRead(buf, std::move(handler));
        });
    }

    void unread(gsl::span<const std::uint8_t> bytes) final
    {
        m_ctx.exec(
          [this, bytes = std::vector(bytes.begin(), bytes.end())]() mutable {
              m_buf.insert(m_buf.end(), bytes.rbegin(), bytes.rend());
          },
          Executor::ExecMode::ImmediatelyIfPossible);
    }

private:
    void startRead(gsl::span<std::uint8_t> outBuf, IOHandler handler)
    {
        const auto askSize = outBuf.size();
        const auto currentBufferSize = m_buf.size();

        const auto readyBufferSize = std::min(currentBufferSize, askSize);
        if (readyBufferSize != 0) {
            const auto bufSpan = gsl::span(m_buf).last(readyBufferSize);
            std::copy(bufSpan.rbegin(), bufSpan.rend(), outBuf.begin());
            m_buf.resize(currentBufferSize - (readyBufferSize));
        }
        const auto leftReadChunk = askSize - readyBufferSize;
        if (leftReadChunk == 0) {
            handler(std::error_code(), askSize);
            return;
        }

        auto next = outBuf.subspan(readyBufferSize);
        m_reader->read(next, [this, aoCtx = AOContextRef(m_ctx), alreadyRead = readyBufferSize,
                              handler = std::move(handler)](auto err, auto size) mutable {
            aoCtx.exec(
              [this, err, size, alreadyRead, handler = std::move(handler)] {
                  if (err) {
                      handler(err, alreadyRead + size);
                      return;
                  }

                  if (size == 0) {
                      handler(std::error_code(), alreadyRead);
                      return;
                  }

                  handler(err, alreadyRead + size);
              },
              Executor::ExecMode::ImmediatelyIfPossible);
        });
    }

    ReaderPtr m_reader;
    std::vector<uint8_t> m_buf;
    AOContext m_ctx;
};

}   // namespace

PushbackReaderPtr PushbackReader::create(AOContext& aoCtx, ReaderPtr reader)
{
    return std::make_unique<PushBackReaderImpl>(aoCtx, std::move(reader));
}

}   // namespace nhope