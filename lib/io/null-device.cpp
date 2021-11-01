#include <memory>
#include <system_error>

#include "nhope/io/null-device.h"

namespace nhope {

namespace {

class NullDeviceImpl final : public NullDevice
{
public:
    explicit NullDeviceImpl(AOContext& parent)
      : m_aoCtx(parent)
    {}

    ~NullDeviceImpl() override
    {
        m_aoCtx.close();
    }

    void write(gsl::span<const std::uint8_t> data, IOHandler handler) override
    {
        m_aoCtx.exec([this, n = data.size(), handler = std::move(handler)] {
            handler(nullptr, n);
        });
    }

    void read(gsl::span<std::uint8_t> /*buf*/, IOHandler handler) override
    {
        m_aoCtx.exec([this, handler = std::move(handler)] {
            handler(nullptr, 0);
        });
    }

private:
    AOContext m_aoCtx;
};

}   // namespace

NullDevicePtr NullDevice::create(AOContext& aoCtx)
{
    return std::make_unique<NullDeviceImpl>(aoCtx);
}

}   // namespace nhope
