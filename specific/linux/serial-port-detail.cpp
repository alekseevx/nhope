#include "sys/ioctl.h"
#include <array>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <unistd.h>

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

void clearReadBuffer(asio::serial_port& serialPort)
{
    const auto hander = serialPort.native_handle();
    std::array<char, 1> buf{};
    ssize_t r{};
    while (true) {
        r = ::read(hander, buf.data(), 1);
        if (r == 0) {
            throw std::runtime_error("serial port EOF");
        }
        if (r < 0) {
            if (errno != EAGAIN) {
                throw std::runtime_error(strerror(errno));
            }
            return;
        }
    }
}

}   // namespace nhope::detail
