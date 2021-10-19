#include <cstddef>
#include <cstring>
#include <memory>
#include <string>
#include <system_error>
#include <utility>

#include <gsl/span>

#include "nhope/async/ao-context.h"
#include "nhope/io/string-reader.h"

namespace nhope {

namespace {

class StringReaderImpl final : public StringReader
{
public:
    StringReaderImpl(AOContext& parent, std::string str)
      : m_aoCtx(parent)
      , m_str(std::move(str))
    {}

    ~StringReaderImpl() override
    {
        m_aoCtx.close();
    }

    void read(gsl::span<std::uint8_t> buf, IOHandler handler) override
    {
        m_aoCtx.exec([this, buf, handler = std::move(handler)] {
            const auto tail = gsl::span(m_str).subspan(m_pos);
            const auto n = std::min(tail.size(), buf.size());
            if (n == 0) {
                handler(std::error_code(), 0);
                return;
            }

            std::memcpy(buf.data(), tail.data(), n);
            m_pos += n;

            handler(std::error_code(), n);
        });
    }

private:
    AOContext m_aoCtx;

    const std::string m_str;
    std::size_t m_pos = 0;
};

}   // namespace

StringReaderPtr StringReader::create(AOContext& aoCtx, std::string string)
{
    return std::make_unique<StringReaderImpl>(aoCtx, std::move(string));
}

}   // namespace nhope
