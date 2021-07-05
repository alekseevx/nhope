#include <cstddef>
#include <cstdint>
#include <exception>
#include <memory>
#include <stdexcept>
#include <system_error>
#include <utility>

#include "asio/error_code.hpp"
#include "asio/serial_port.hpp"

#include "nhope/io/detail/asio-device.h"

#include "nhope/io/serial-port.h"

namespace nhope {

SerialPortError::SerialPortError(std::error_code err)
  : IoError(err)
{}

using SerialPort = detail::AsioDevice<asio::serial_port>;
namespace {

void configure(SerialPort& x, const SerialPortSettings& s)
{
    using asio::serial_port;
    auto& serial = x.impl();

    asio::error_code errCode;
    serial.open(s.portName, errCode);
    if (errCode) {
        throw SerialPortError(errCode);
    }

    auto baudRate = serial_port::baud_rate(static_cast<int>(SerialPortSettings::BaudRate::Baud115200));
    if (s.baudrate.has_value()) {
        baudRate = serial_port::baud_rate(static_cast<int>(s.baudrate.value()));
    }
    serial_port::parity parity;
    if (s.parity.has_value()) {
        switch (s.parity.value()) {
        case SerialPortSettings::Parity::NoParity:
            parity = serial_port::parity(serial_port::parity::none);
            break;
        case SerialPortSettings::Parity::EvenParity:
            parity = serial_port::parity(serial_port::parity::even);
            break;
        case SerialPortSettings::Parity::OddParity:
            parity = serial_port::parity(serial_port::parity::odd);
            break;
        }
    }
    serial_port::flow_control flow;
    if (s.flow.has_value()) {
        switch (s.flow.value()) {
        case SerialPortSettings::FlowControl::NoFlowControl:
            flow = serial_port::flow_control(serial_port::flow_control::none);
            break;
        case SerialPortSettings::FlowControl::HardwareControl:
            flow = serial_port::flow_control(serial_port::flow_control::hardware);
            break;
        case SerialPortSettings::FlowControl::SoftwareControl:
            flow = serial_port::flow_control(serial_port::flow_control::software);
            break;
        }
    }

    serial_port::character_size characterSize;
    if (s.databits.has_value()) {
        characterSize = serial_port::character_size(static_cast<int>(s.databits.value()));
    }
    serial_port::stop_bits stop;
    if (s.stop.has_value()) {
        switch (s.stop.value()) {
        case SerialPortSettings::StopBits::OneAndHalfStop:
            stop = serial_port::stop_bits(serial_port::stop_bits::onepointfive);
            break;
        case SerialPortSettings::StopBits::OneStop:
            stop = serial_port::stop_bits(serial_port::stop_bits::one);
            break;
        case SerialPortSettings::StopBits::TwoStop:
            stop = serial_port::stop_bits(serial_port::stop_bits::two);
            break;
        }
    }
    serial.set_option(baudRate);
    serial.set_option(parity);
    serial.set_option(flow);
    serial.set_option(characterSize);
    serial.set_option(stop);
}
}   // namespace

std::unique_ptr<IoDevice> openSerialPort(nhope::Executor& executor, const SerialPortSettings& settings)
{
    auto dev = std::make_unique<SerialPort>(executor);
    configure(*dev, settings);
    return dev;
}

}   // namespace nhope