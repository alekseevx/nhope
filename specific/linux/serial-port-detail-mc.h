#pragma once

#include "sys/ioctl.h"
#include "nhope/io/serial-port.h"

namespace nhope::detail {
inline nhope::SerialPortParams::ModemControl toModemControl(int arg)
{
    switch (arg) {
    case TIOCM_LE:
        return nhope::SerialPortParams::ModemControl::DSR;
    case TIOCM_DTR:
        return nhope::SerialPortParams::ModemControl::DTR;
    case TIOCM_RTS:
        return nhope::SerialPortParams::ModemControl::RTS;
    case TIOCM_ST:
        return nhope::SerialPortParams::ModemControl::STXD;
    case TIOCM_SR:
        return nhope::SerialPortParams::ModemControl::SRXD;
    case TIOCM_CTS:
        return nhope::SerialPortParams::ModemControl::CTS;
    case TIOCM_CAR:
        return nhope::SerialPortParams::ModemControl::DCD;
    case TIOCM_RNG:
        return nhope::SerialPortParams::ModemControl::RNG;
    default:
        throw std::logic_error("serial-port: unknown modem control");
    }
}
}   // namespace nhope::detail
