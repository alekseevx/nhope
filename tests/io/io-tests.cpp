#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <system_error>
#include <thread>
#include <variant>
#include <vector>

#include <asio/io_context.hpp>
#include <gsl/span_ext>
#include <gsl/span>
#include <gtest/gtest.h>

#include "nhope/async/ao-context-error.h"
#include "nhope/async/ao-context.h"
#include "nhope/async/event.h"
#include "nhope/async/lockable-value.h"
#include "nhope/async/thread-executor.h"
#include "nhope/io/detail/asio-device-wrapper.h"
#include "nhope/io/file.h"
#include "nhope/io/io-device.h"
#include "nhope/io/serial-port.h"
#include "nhope/io/tcp.h"

#include "./test-helpers/tcp-echo-server.h"

namespace {
using namespace nhope;
using namespace std::literals;

class AsioStub final
{
public:
    struct ReadOp
    {
        template<typename Buf>
        ReadOp(std::size_t bufSize, const Buf& readData)
          : bufSize(bufSize)
          , readData(readData.begin(), readData.end())
        {}

        ReadOp(std::size_t bufSize, std::errc errc)
          : bufSize(bufSize)
          , error(std::make_error_code(errc))
        {}

        // arguments
        std::size_t bufSize = 0;

        // results
        std::error_code error;
        std::vector<std::uint8_t> readData;
    };

    struct WriteOp
    {
        template<typename Buf>
        WriteOp(const Buf& buf, std::size_t writeSize)
          : buf(buf.begin(), buf.end())
          , writeSize(writeSize)
        {}

        template<typename Buf>
        WriteOp(const Buf& buf, std::errc errc)
          : buf(buf.begin(), buf.end())
          , error(std::make_error_code(errc))
        {}

        // argument
        std::vector<std::uint8_t> buf;

        // result
        std::error_code error;
        std::size_t writeSize = 0;
    };

    struct CloseOp
    {};

    using Operation = std::variant<CloseOp, ReadOp, WriteOp>;
    using Operations = std::list<Operation>;

    explicit AsioStub(asio::io_context& ctx)
      : m_ioCtx(ctx)
    {}

    template<typename Buff, typename Handler>
    void async_read_some(const Buff& buffer,   // NOLINT(readability-identifier-naming)
                         Handler&& handler)
    {
        m_ioCtx.post([this, buffer, handler]() mutable {
            const auto op = nextOperation<ReadOp>();
            EXPECT_EQ(op.bufSize, buffer.size());
            std::memcpy(buffer.data(), op.readData.data(), op.readData.size());
            handler(op.error, op.readData.size());
        });
    }

    template<typename Buff, typename Handler>
    void async_write_some(const Buff& buffer,   // NOLINT(readability-identifier-naming)
                          Handler&& handler)
    {
        m_ioCtx.post([this, buffer, handler]() mutable {
            const auto op = nextOperation<WriteOp>();
            EXPECT_EQ(op.buf.size(), buffer.size());

            EXPECT_TRUE(std::memcmp(op.buf.data(), buffer.data(), buffer.size()) == 0);
            handler(op.error, op.writeSize);
        });
    }

    void close()
    {
        nextOperation<CloseOp>();
    }

    void setOperations(const Operations& actions)
    {
        m_operations = actions;
    }

private:
    template<typename OpT>
    OpT nextOperation()
    {
        auto actions = m_operations.writeAccess();
        EXPECT_TRUE(!actions->empty());
        auto action = actions->front();
        actions->pop_front();

        EXPECT_TRUE(std::holds_alternative<OpT>(action));
        return std::get<OpT>(action);
    }

    asio::io_context& m_ioCtx;
    LockableValue<std::list<Operation>> m_operations;
};

class StubDevice final : public detail::AsioDeviceWrapper<IODevice, AsioStub>
{
public:
    explicit StubDevice(AOContext& parent, const AsioStub::Operations& operations)
      : detail::AsioDeviceWrapper<IODevice, AsioStub>(parent)
    {
        asioDev.setOperations(operations);
    }
};

template<typename... C>
std::vector<std::uint8_t> concat(const C&... c)
{
    std::vector<std::uint8_t> retval;
    (retval.insert(retval.end(), c.begin(), c.end()), ...);
    return retval;
}

}   // namespace

TEST(IOTest, AsioDeviceWrapper_Read)   // NOLINT
{
    constexpr std::size_t bufSize = 1024;
    static const auto etalonData = std::vector<std::uint8_t>(70, 0xFE);

    ThreadExecutor executor;
    AOContext aoCtx(executor);
    StubDevice dev(aoCtx, AsioStub::Operations{
                            AsioStub::ReadOp{bufSize, etalonData},
                            AsioStub::CloseOp{},
                          });

    Event finished;

    std::vector<std::uint8_t> buf(bufSize);
    dev.read(buf, [&](std::error_code err, std::size_t size) {
        EXPECT_TRUE(aoCtx.workInThisThread());
        EXPECT_FALSE(err);
        EXPECT_EQ(size, etalonData.size());

        const auto data = gsl::span<const std::uint8_t>(buf).first(size);
        EXPECT_EQ(data, gsl::span(etalonData));

        finished.set();
    });

    finished.wait();
}

TEST(IOTest, AsioDeviceWrapper_Write)   // NOLINT
{
    const auto etalonData = std::vector<std::uint8_t>(70, 0xFE);
    constexpr std::size_t writeSize = 17;

    ThreadExecutor executor;
    AOContext aoCtx(executor);
    StubDevice dev(aoCtx, AsioStub::Operations{
                            AsioStub::WriteOp{etalonData, writeSize},
                            AsioStub::CloseOp{},
                          });

    Event finished;

    dev.write(etalonData, [&](std::error_code err, std::size_t size) {
        EXPECT_TRUE(aoCtx.workInThisThread());
        EXPECT_FALSE(err);
        EXPECT_EQ(size, writeSize);

        finished.set();
    });

    finished.wait();
}

TEST(IOTest, Read)   // NOLINT
{
    constexpr std::size_t bytesCount = 1024;
    const auto etalonData = std::vector<std::uint8_t>(70, 0xFE);

    ThreadExecutor executor;
    AOContext aoCtx(executor);
    StubDevice dev(aoCtx, {
                            AsioStub::ReadOp{bytesCount, etalonData},
                            AsioStub::CloseOp{},
                          });

    const auto data = nhope::read(dev, bytesCount).get();
    EXPECT_EQ(data, etalonData);
}

TEST(IOTest, ReadFailed)   // NOLINT
{
    constexpr std::size_t bytesCount = 1024;

    ThreadExecutor executor;
    AOContext aoCtx(executor);
    StubDevice dev(aoCtx, {
                            AsioStub::ReadOp{bytesCount, std::errc::io_error},
                            AsioStub::CloseOp{},
                          });

    EXPECT_THROW(nhope::read(dev, bytesCount).get(), std::system_error);   // NOLINT
}

TEST(IOTest, Write)   // NOLINT
{
    const auto data = std::vector<std::uint8_t>(70, 0xFE);
    constexpr std::size_t writeSize = 17;

    ThreadExecutor executor;
    AOContext aoCtx(executor);
    StubDevice dev(aoCtx, {
                            AsioStub::WriteOp{data, writeSize},
                            AsioStub::CloseOp{},
                          });

    const auto written = write(dev, data).get();
    EXPECT_EQ(writeSize, written);
}

TEST(IOTest, WriteFailed)   // NOLINT
{
    const auto data = std::vector<std::uint8_t>(70, 0xFE);

    ThreadExecutor executor;
    AOContext aoCtx(executor);
    StubDevice dev(aoCtx, {
                            AsioStub::WriteOp{data, std::errc::io_error},
                            AsioStub::CloseOp{},
                          });

    EXPECT_THROW(write(dev, data).get(), std::system_error);   // NOLINT
}

TEST(IOTest, ReadExactly)   // NOLINT
{
    const auto firstPart = std::vector<std::uint8_t>(70, 0xFD);
    const auto secondPart = std::vector<std::uint8_t>(70, 0xFE);
    const auto etalonData = concat(firstPart, secondPart);

    ThreadExecutor executor;
    AOContext aoCtx(executor);
    StubDevice dev(aoCtx, {
                            // NOLINTNEXTLINE
                            AsioStub::ReadOp{140, gsl::span(etalonData).subspan(0, 30)},
                            // NOLINTNEXTLINE
                            AsioStub::ReadOp{110, gsl::span(etalonData).subspan(30, 30)},
                            // NOLINTNEXTLINE
                            AsioStub::ReadOp{80, gsl::span(etalonData).subspan(60, 30)},
                            // NOLINTNEXTLINE
                            AsioStub::ReadOp{50, gsl::span(etalonData).subspan(90, 30)},
                            // NOLINTNEXTLINE
                            AsioStub::ReadOp{20, gsl::span(etalonData).subspan(120, 20)},
                            AsioStub::CloseOp{},
                          });

    const auto data = nhope::readExactly(dev, 140).get();
    EXPECT_EQ(data, etalonData);
}

TEST(IOTest, ReadExactlyFailed)   // NOLINT
{
    const auto firstPart = std::vector<std::uint8_t>(70, 0xFD);
    const auto secondPart = std::vector<std::uint8_t>(70, 0xFE);
    const auto etalonData = concat(firstPart, secondPart);

    ThreadExecutor executor;
    AOContext aoCtx(executor);
    StubDevice dev(aoCtx, {
                            // NOLINTNEXTLINE
                            AsioStub::ReadOp{140, gsl::span(etalonData).subspan(0, 30)},
                            // NOLINTNEXTLINE
                            AsioStub::ReadOp{110, std::errc::io_error},
                            AsioStub::CloseOp{},
                          });

    // NOLINTNEXTLINE
    EXPECT_THROW(nhope::readExactly(dev, 140).get(), std::system_error);
}

TEST(IOTest, WriteExactly)   // NOLINT
{
    const auto firstPart = std::vector<std::uint8_t>(70, 0xFD);
    const auto secondPart = std::vector<std::uint8_t>(70, 0xFE);
    const auto data = concat(firstPart, secondPart);

    ThreadExecutor executor;
    AOContext aoCtx(executor);
    StubDevice dev(aoCtx, {
                            // NOLINTNEXTLINE
                            AsioStub::WriteOp{data, 30},
                            // NOLINTNEXTLINE
                            AsioStub::WriteOp{gsl::span(data).last(110), 30},
                            // NOLINTNEXTLINE
                            AsioStub::WriteOp{gsl::span(data).last(80), 30},
                            // NOLINTNEXTLINE
                            AsioStub::WriteOp{gsl::span(data).last(50), 30},
                            // NOLINTNEXTLINE
                            AsioStub::WriteOp{gsl::span(data).last(20), 20},
                            AsioStub::CloseOp{},
                          });

    EXPECT_EQ(nhope::writeExactly(dev, data).get(), data.size());
}

TEST(IOTest, WriteExactlyFailed)   // NOLINT
{
    const auto firstPart = std::vector<std::uint8_t>(70, 0xFD);
    const auto secondPart = std::vector<std::uint8_t>(70, 0xFE);
    const auto data = concat(firstPart, secondPart);

    ThreadExecutor executor;
    AOContext aoCtx(executor);
    StubDevice dev(aoCtx, {
                            // NOLINTNEXTLINE
                            AsioStub::WriteOp{data, 30},
                            // NOLINTNEXTLINE
                            AsioStub::WriteOp{gsl::span(data).last(110), std::errc::io_error},
                            AsioStub::CloseOp{},
                          });

    // NOLINTNEXTLINE
    EXPECT_THROW(nhope::writeExactly(dev, data).get(), std::system_error);
}

TEST(IOTest, readLine)   // NOLINT
{
    ThreadExecutor executor;
    AOContext aoCtx(executor);

    // data: "1\n23\n"
    StubDevice dev(aoCtx, {
        // "1" line
        AsioStub::ReadOp{1, "1"sv},
#if WIN32
          AsioStub::ReadOp{1, "\r"sv},
#endif
          AsioStub::ReadOp{1, "\n"sv},
          // "23" line
          AsioStub::ReadOp{1, "2"sv},   //
          AsioStub::ReadOp{1, "3"sv},
#if WIN32
          AsioStub::ReadOp{1, "\r"sv},
#endif
          AsioStub::ReadOp{1, "\n"sv},

          // empty line
          AsioStub::ReadOp{1, ""sv},

          AsioStub::CloseOp{},
    });

    // NOLINTNEXTLINE
    EXPECT_EQ(nhope::readLine(dev).get(), "1"sv);
    EXPECT_EQ(nhope::readLine(dev).get(), "23"sv);
    EXPECT_EQ(nhope::readLine(dev).get(), ""sv);
}

TEST(IOTest, readFile)   // NOLINT
{
    ThreadExecutor executor;
    AOContext aoCtx(executor);

    const auto thisFileData = File::readAll(aoCtx, __FILE__).get();
    EXPECT_EQ(thisFileData.size(), std::filesystem::file_size(__FILE__));
}

TEST(IOTest, writeFile)   // NOLINT
{
    ThreadExecutor executor;
    AOContext aoCtx(executor);

    const auto thisFileData = File::readAll(aoCtx, __FILE__).get();

    auto dev = File::open(aoCtx, "temp-file", OpenFileMode::WriteOnly);
    EXPECT_EQ(writeExactly(*dev, thisFileData).get(), std::filesystem::file_size(__FILE__));   // NOLINT
    dev.reset();

    const auto tempFileData = File::readAll(aoCtx, "temp-file").get();
    EXPECT_EQ(thisFileData, tempFileData);
}

TEST(IOTest, openNotExistFile)   // NOLINT
{
    ThreadExecutor executor;
    AOContext aoCtx(executor);

    // NOLINTNEXTLINE
    EXPECT_THROW(File::open(aoCtx, "not-exist-file", OpenFileMode::ReadOnly), std::system_error);
}

TEST(IOTest, invalidFileOpenMode)   // NOLINT
{
    ThreadExecutor executor;
    AOContext aoCtx(executor);

    // NOLINTNEXTLINE
    EXPECT_THROW(File::open(aoCtx, "temp-file", static_cast<OpenFileMode>(10000)), std::logic_error);
}

TEST(IOTest, tcpReadWrite)   //NOLINT
{
    constexpr auto dataSize = 512 * 1024;

    test::TcpEchoServer echoServer;
    ThreadExecutor e;
    AOContext aoCtx(e);

    auto conDev = TcpSocket::connect(aoCtx, test::TcpEchoServer::srvAddress, test::TcpEchoServer::srvPort).get();

    std::vector<uint8_t> data(dataSize, 1);
    EXPECT_EQ(writeExactly(*conDev, data).get(), dataSize);
    const auto readded = readExactly(*conDev, dataSize).get();
    EXPECT_EQ(readded, data);
}

TEST(IOTest, hostNameResolveFailed)   // NOLINT
{
    ThreadExecutor e;
    AOContext aoCtx(e);

    auto conDev = TcpSocket::connect(aoCtx, "invalid-host-name!!!!!!!", test::TcpEchoServer::srvPort);
    EXPECT_THROW(conDev.get(), std::runtime_error);   // NOLINT
}

TEST(IOTest, connectFailed)   // NOLINT
{
    ThreadExecutor e;
    AOContext aoCtx(e);

    auto conDev = TcpSocket::connect(aoCtx, test::TcpEchoServer::srvAddress, test::TcpEchoServer::srvPort);
    EXPECT_THROW(conDev.get(), std::runtime_error);   // NOLINT
}

TEST(IOTest, cancelConnect)   // NOLINT
{
    ThreadExecutor e;
    AOContext aoCtx(e);

    auto conDev = TcpSocket::connect(aoCtx, test::TcpEchoServer::srvAddress, test::TcpEchoServer::srvPort);
    aoCtx.close();

    // NOLINTNEXTLINE
    EXPECT_THROW(conDev.get(), AsyncOperationWasCancelled);
}

TEST(IOTest, openNotExistSerialPort)   // NOLINT
{
    ThreadExecutor e;
    AOContext aoCtx(e);

    // NOLINTNEXTLINE
    EXPECT_THROW(SerialPort::open(aoCtx, "not-exist-serial-port", SerialPortParams{}), std::system_error);
}

TEST(IOTest, SerialPort_AvailableDevices)   // NOLINT
{
    const auto ports = SerialPort::availableDevices();
    for (const auto& portName : ports) {
        EXPECT_TRUE(std::filesystem::exists(portName));
    }
}
