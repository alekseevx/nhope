#include "sys/ioctl.h"
#include <cstdio>
#include <stdexcept>

#include "nhope/io/detail/serial-port-detail.h"

namespace nhope::detail {

void setRTS(asio::serial_port& serialPort, bool state)
{
    int arg = TIOCM_RTS;
    const auto hander = serialPort.native_handle();
    if (ioctl(hander, state ? TIOCMBIS : TIOCMBIC, &arg) == -1) {
        const auto err = std::error_code(errno, std::system_category());
        throw std::system_error(err, "serial-port: failed to set RTS");
    }
}

void setDTR(asio::serial_port& serialPort, bool state)
{
    int arg = TIOCM_DTR;
    const auto hander = serialPort.native_handle();
    if (ioctl(hander, state ? TIOCMBIS : TIOCMBIC, &arg) == -1) {
        const auto err = std::error_code(errno, std::system_category());
        throw std::system_error(err, "serial-port: failed to set DTR");
    }
}

SerialPortParams::ModemControl getModemControl(asio::serial_port& serialPort)
{
    SerialPortParams::ModemControl arg{};
    const auto hander = serialPort.native_handle();
    if (ioctl(hander, TIOCMGET, &arg) == -1) {
        const auto err = std::error_code(errno, std::system_category());
        throw std::system_error(err, "serial-port: failed to get modem control");
    }
    return arg;
}

}   // namespace nhope::detail
