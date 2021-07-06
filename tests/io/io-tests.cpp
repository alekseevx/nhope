#include <cstdint>
#include <chrono>
#include <string>
#include <thread>
#include <vector>
#include <filesystem>

#include <gtest/gtest.h>

#include "asio/io_context.hpp"
#include "nhope/async/thread-executor.h"
#include "nhope/io/io-device.h"
#include "nhope/io/file.h"
#include "nhope/io/serial-port.h"
#include "nhope/io/tcp.h"

#include "test-helpers/io-stub.h"

using namespace nhope;
using namespace std::literals;

namespace {
TcpEchoServer echoServer;
}

TEST(IoTest, ReadWrite)   // NOLINT
{
    constexpr int count = 100;
    StubDevice dev;
    auto data = read(dev, count).get();
    EXPECT_LE(data.size(), count);
    auto writeCount = write(dev, data).get();
    EXPECT_LE(data.size(), writeCount);
}

TEST(IoTest, ReadExactly)   // NOLINT
{
    constexpr int count = 100;
    StubDevice dev;
    auto data = readExactly(dev, count).get();

    EXPECT_EQ(data.size(), count);
}

TEST(IoTest, WriteExactly)   // NOLINT
{
    constexpr int count = 100;
    StubDevice dev;
    std::vector<uint8_t> data(count);
    auto size = writeExactly(dev, data).get();

    EXPECT_EQ(size, count);
}

TEST(IoTest, ReadFile)   // NOLINT
{
    nhope::ThreadExecutor e;
    FileSettings s;
    s.fileName = __FILE__;
    s.mode = nhope::FileMode::ReadOnly;
    auto dev = openFile(e, s);

    const std::string firstLineThisFile = "#include <cstdint>";
    const std::string secondLineThisFile = "#include <chrono>";

    std::vector<uint8_t> data(4);
    EXPECT_THROW(writeExactly(*dev, data).get(), IoError);   // NOLINT
    const auto readded = readLine(*dev).get();
    EXPECT_EQ(readded, firstLineThisFile);
    const auto readded2 = readLine(*dev).get();
    EXPECT_EQ(readded2, secondLineThisFile);

    auto devAll = openFile(e, s);
    const auto thisFileData = readAll(*devAll).get();
    EXPECT_EQ(thisFileData.size(), std::filesystem::file_size(s.fileName));

    auto thisFile = readFile(s.fileName, e).get();
    EXPECT_EQ(thisFileData, thisFile);
}

TEST(IoTest, WriteFile)   // NOLINT
{
    nhope::ThreadExecutor e;
    FileSettings s;
    s.fileName = "temp";
    s.mode = nhope::FileMode::WriteOnly;
    auto dev = openFile(e, s);

    std::vector<uint8_t> data(4, 4);
    const auto count = writeExactly(*dev, data).get();   // NOLINT
    dev.reset();
    s.mode = nhope::FileMode::ReadWrite;
    auto devRW = openFile(e, s);
    const auto readed = readExactly(*devRW, 4).get();
    EXPECT_EQ(readed, data);

    EXPECT_EQ(count, data.size());
}

TEST(IoTest, Exception)   // NOLINT
{
    StubDevice dev;
    std::vector<uint8_t> data(badSize);
    EXPECT_THROW(writeExactly(dev, data).get(), IoError);     // NOLINT
    EXPECT_THROW(readExactly(dev, badSize).get(), IoError);   // NOLINT

    constexpr auto bigSize{1024};
    auto rf = readExactly(dev, bigSize);
    std::this_thread::sleep_for(200ms);
    dev.close();
    EXPECT_THROW(rf.get(), IoError);   // NOLINT
    dev.open();

    auto rl = readLine(dev);
    std::this_thread::sleep_for(200ms);
    dev.close();
    EXPECT_THROW(rl.get(), IoError);   // NOLINT

    dev.open();
    auto ral = readAll(dev);
    std::this_thread::sleep_for(200ms);
    dev.close();
    EXPECT_THROW(ral.get(), IoError);   // NOLINT

    nhope::ThreadExecutor e;

    FileSettings s;
    s.fileName = "NotExisted";
    s.mode = nhope::FileMode::ReadOnly;
    EXPECT_THROW(openFile(e, s), IoError);   // NOLINT

    SerialPortSettings settings{};
    settings.portName = s.fileName;

    // EXPECT_THROW(openSerialPort(e, settings), SerialPortError);   // NOLINT
    settings.portName = "~/.profile";
    EXPECT_THROW(openSerialPort(e, settings), SerialPortError);
    // d->executor();
}

TEST(IoTest, Asio)   //NOLINT
{
    nhope::ThreadExecutor e;
    AsioStubDev dev(e);
    auto& impl = dev.impl();
    asio::error_code erc;
    impl.open(erc);
    EXPECT_EQ(readExactly(dev, 3).get().size(), 3);
    EXPECT_THROW(readExactly(dev, badSize).get(), IoError);   //NOLINT

    std::vector<uint8_t> data(3);

    EXPECT_EQ(writeExactly(dev, data).get(), 3);
    data.resize(badSize);
    EXPECT_THROW(writeExactly(dev, data).get(), IoError);   //NOLINT
}

TEST(IoTest, Tcp)   //NOLINT
{
    nhope::ThreadExecutor e;

    constexpr auto dataSize{9064};

    auto conDev = connect(e, echoSettings).get();
    std::vector<uint8_t> data(dataSize, 1);
    EXPECT_EQ(writeExactly(*conDev, data).get(), dataSize);
    const auto readded = readExactly(*conDev, dataSize).get();
    EXPECT_EQ(readded, data);

    TcpClientParam badSettings;
    badSettings.endpoint.host = "false";
    auto badConnect = connect(e, badSettings);

    try {
        badConnect.get();
        FAIL() << "can`t";
    } catch (const TcpError& e) {
        ASSERT_TRUE(e.code());
    }
    badSettings.endpoint.host = "localhost";
    badSettings.endpoint.port = 55766;
    EXPECT_THROW(connect(e, badSettings).get(), TcpError);   // NOLINT
}