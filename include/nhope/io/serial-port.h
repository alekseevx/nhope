#pragma once

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <system_error>

#include "nhope/io/io-device.h"

#include "nhope/async/executor.h"
#include "nhope/async/future.h"

namespace nhope {

class SerialPortError final : public IoError
{
public:
    explicit SerialPortError(std::error_code err);
};

struct SerialPortSettings
{
    enum class BaudRate
    {
        Baud1200 = 1200,
        Baud2400 = 2400,
        Baud4800 = 4800,
        Baud9600 = 9600,
        Baud19200 = 19200,
        Baud38400 = 38400,
        Baud57600 = 57600,
        Baud115200 = 115200
    };

    enum class DataBits
    {
        Data5 = 5,
        Data6 = 6,
        Data7 = 7,
        Data8 = 8
    };

    enum class Parity
    {
        NoParity = 0,
        EvenParity = 2,
        OddParity = 3
    };

    enum class StopBits
    {
        OneStop = 1,
        OneAndHalfStop = 3,
        TwoStop = 2
    };

    enum class FlowControl
    {
        NoFlowControl,
        HardwareControl,
        SoftwareControl
    };

    std::string portName;
    std::optional<BaudRate> baudrate;
    std::optional<DataBits> databits;
    std::optional<Parity> parity;
    std::optional<StopBits> stop;
    std::optional<FlowControl> flow;
};

std::unique_ptr<IoDevice> openSerialPort(nhope::Executor& executor, const SerialPortSettings& settings);

}   // namespace nhope