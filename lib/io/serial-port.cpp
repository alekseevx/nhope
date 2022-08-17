#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>

#include <asio/serial_port.hpp>
#include <fmt/format.h>

#include "nhope/io/detail/asio-device-wrapper.h"
#include "nhope/io/detail/serial-port-detail.h"
#include "nhope/io/serial-port.h"

namespace nhope {

namespace {
using namespace detail;
using serial_port = asio::serial_port;

serial_port::baud_rate toAsio(std::optional<SerialPortParams::BaudRate> baudrateOpt)
{
    const auto baudrate = baudrateOpt.value_or(SerialPortParams::BaudRate::Baud115200);
    return serial_port::baud_rate(static_cast<int>(baudrate));
}

serial_port::parity toAsio(std::optional<SerialPortParams::Parity> parityOpt)
{
    if (!parityOpt.has_value()) {
        return serial_port::parity();
    }

    switch (parityOpt.value()) {
    case SerialPortParams::Parity::NoParity:
        return serial_port::parity(serial_port::parity::none);
    case SerialPortParams::Parity::EvenParity:
        return serial_port::parity(serial_port::parity::even);
    case SerialPortParams::Parity::OddParity:
        return serial_port::parity(serial_port::parity::odd);
    default:
        throw std::logic_error("Invalid Parity option");
    }
}

serial_port::flow_control toAsio(std::optional<SerialPortParams::FlowControl> flowOpt)
{
    if (!flowOpt.has_value()) {
        return serial_port::flow_control();
    }

    switch (flowOpt.value()) {
    case SerialPortParams::FlowControl::NoFlowControl:
        return serial_port::flow_control(serial_port::flow_control::none);
    case SerialPortParams::FlowControl::HardwareControl:
        return serial_port::flow_control(serial_port::flow_control::hardware);
    case SerialPortParams::FlowControl::SoftwareControl:
        return serial_port::flow_control(serial_port::flow_control::software);
    default:
        throw std::logic_error("Invalid FlowControl option");
    }
}

serial_port::character_size toAsio(std::optional<SerialPortParams::DataBits> databitsOpt)
{
    const auto databits = databitsOpt.value_or(SerialPortParams::DataBits::Data8);
    return serial_port::character_size(static_cast<int>(databits));
}

serial_port::stop_bits toAsio(std::optional<SerialPortParams::StopBits> stopbitsOpt)
{
    if (!stopbitsOpt.has_value()) {
        return serial_port::stop_bits();
    }

    switch (stopbitsOpt.value()) {
    case SerialPortParams::StopBits::OneAndHalfStop:
        return serial_port::stop_bits(serial_port::stop_bits::onepointfive);
    case SerialPortParams::StopBits::OneStop:
        return serial_port::stop_bits(serial_port::stop_bits::one);
    case SerialPortParams::StopBits::TwoStop:
        return serial_port::stop_bits(serial_port::stop_bits::two);
    default:
        throw std::logic_error("Invalid StopBits option");
    }
}

class SerialPortImpl final : public detail::AsioDeviceWrapper<SerialPort, serial_port>
{
public:
    explicit SerialPortImpl(nhope::AOContext& aoCtx, std::string_view device, const SerialPortParams& params)
      : detail::AsioDeviceWrapper<SerialPort, serial_port>(aoCtx)
    {
        std::error_code errCode;
        asioDev.open(std::string(device), errCode);
        if (errCode) {
            throw std::system_error(errCode, fmt::format("Unable to open serial port '{}'", device));
        }

        asioDev.set_option(toAsio(params.baudrate));
        asioDev.set_option(toAsio(params.parity));
        asioDev.set_option(toAsio(params.flow));
        asioDev.set_option(toAsio(params.databits));
        asioDev.set_option(toAsio(params.stopbits));
    }

    void clearReadBuffer() override
    {
        nhope::detail::clearReadBuffer(asioDev);
    }

    void setRTS(bool state) override
    {
        nhope::detail::setRTS(asioDev, state);
    }

    void setDTR(bool state) override
    {
        nhope::detail::setDTR(asioDev, state);
    }

    SerialPortParams::ModemControl getModemControl() override
    {
        return nhope::detail::getModemControl(asioDev);
    }
};

}   // namespace

SerialPortPtr SerialPort::open(nhope::AOContext& aoCtx, std::string_view device, const SerialPortParams& params)
{
    return std::make_unique<SerialPortImpl>(aoCtx, device, params);
}

}   // namespace nhope
