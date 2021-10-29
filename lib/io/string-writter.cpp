#include <memory>
#include <string>

#include "nhope/io/string-writter.h"
#include "nhope/async/ao-context.h"
#include "nhope/io/io-device.h"

namespace nhope {

namespace {

class StringWritterImpl final : public StringWritter
{
public:
    explicit StringWritterImpl(AOContext& parent)
      : m_aoCtx(parent)
    {}

    ~StringWritterImpl() override
    {
        m_aoCtx.close();
    }

    [[nodiscard]] std::string takeContent() override
    {
        return std::move(m_content);
    }

    void write(std::span<const std::uint8_t> data, IOHandler handler) override
    {
        m_aoCtx.exec([this, data, handler = std::move(handler)] {
            // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
            m_content.append(reinterpret_cast<const char*>(data.data()), data.size());

            handler(nullptr, data.size());
        });
    }

private:
    std::string m_content;
    AOContext m_aoCtx;
};

}   // namespace

StringWritterPtr StringWritter::create(AOContext& aoCtx)
{
    return std::make_unique<StringWritterImpl>(aoCtx);
}

}   // namespace nhope
