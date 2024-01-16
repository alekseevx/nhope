#include <stdexcept>
#include "nhope/io/detail/serial-port-detail.h"

namespace nhope::detail {

void setRTS(asio::serial_port& /*serialPort*/, bool /*state*/)
{
    throw std::logic_error("serial-port: platform don't support modem control");
}

void setDTR(asio::serial_port& /*serialPort*/, bool /*state*/)
{
    throw std::logic_error("serial-port: platform don't support modem control");
}

nhope::SerialPortParams::ModemControl getModemControl(asio::serial_port& /*serialPort*/)
{
    throw std::logic_error("serial-port: platform don't support modem control");
}

void clearReadBuffer(asio::serial_port& /*unused*/)
{
    throw std::logic_error("serial-port: clearReadBuffer not implemented yet");
}

}   // namespace nhope::detail
