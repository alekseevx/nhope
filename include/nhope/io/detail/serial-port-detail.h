#pragma once

#include "asio/serial_port.hpp"
#include "nhope/io/serial-port.h"

namespace nhope::detail {

void setRTS(asio::serial_port& serialPort, bool state);
void setDTR(asio::serial_port& serialPort, bool state);
SerialPortParams::ModemControl getModemControl(asio::serial_port& serialPort);

}   // namespace nhope::detail
