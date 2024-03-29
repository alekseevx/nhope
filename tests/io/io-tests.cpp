#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>

#include <system_error>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

#include <asio/io_context.hpp>
#include <gsl/span_ext>
#include <gsl/span>
#include <gtest/gtest.h>

#include "asio/error.hpp"
#include "nhope/async/ao-context-error.h"
#include "nhope/async/ao-context.h"
#include "nhope/async/async-invoke.h"
#include "nhope/async/event.h"
#include "nhope/async/future.h"
#include "nhope/async/lockable-value.h"
#include "nhope/async/thread-executor.h"
#include "nhope/io/bit-seq-reader.h"
#include "nhope/io/detail/asio-device-wrapper.h"
#include "nhope/io/file.h"
#include "nhope/io/io-device.h"
#include "nhope/io/local-socket.h"
#include "nhope/io/null-device.h"
#include "nhope/io/pushback-reader.h"
#include "nhope/io/serial-port.h"
#include "nhope/io/string-reader.h"
#include "nhope/io/string-writter.h"
#include "nhope/io/tcp.h"
#include "nhope/io/udp.h"

#include "./test-helpers/tcp-echo-server.h"
#include "./test-helpers/udp-echo-server.h"

#ifdef WIN32
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include <WinSock2.h>
#include <Ws2tcpip.h>
#include <stdio.h>
#pragma comment(lib, "Ws2_32.lib")
#endif

#ifdef __linux__
#include "./test-helpers/virtual-serial-port.h"
#include <sys/socket.h>
#endif

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

    struct CancelOp
    {};

    using Operation = std::variant<CloseOp, ReadOp, WriteOp, CancelOp>;
    using Operations = std::list<Operation>;

    explicit AsioStub(asio::io_context& ctx)
      : m_ioCtx(ctx)
    {}

    void cancel()
    {
        nextOperation<CancelOp>();
    }

    template<typename Buff, typename Handler>
    void async_read_some(const Buff& buffer,   // NOLINT(readability-identifier-naming)
                         Handler handler)
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
                          Handler handler)
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

class StubGate
  : public IODevice
  , public IOCancellable
{};

class StubDevice final : public detail::AsioDeviceWrapper<StubGate, AsioStub>
{
public:
    explicit StubDevice(AOContext& parent, const AsioStub::Operations& operations)
      : detail::AsioDeviceWrapper<StubGate, AsioStub>(parent)
    {
        asioDev.setOperations(operations);
    }
};

template<typename... C>
std::vector<std::uint8_t> concatContainers(const C&... c)
{
    std::vector<std::uint8_t> retval;
    (retval.insert(retval.end(), c.begin(), c.end()), ...);
    return retval;
}

bool eq(const std::vector<std::uint8_t>& a, std::string_view b)
{
    if (a.size() != b.size()) {
        return false;
    }

    for (std::size_t i = 0; i < a.size(); ++i) {
        if (a[i] != static_cast<std::uint8_t>(b[i])) {
            return false;
        }
    }

    return true;
}

}   // namespace

TEST(IOTest, NullDevice_Write)   // NOLINT
{
    constexpr std::size_t bufSize = 1024;
    const auto etalonData = std::vector<std::uint8_t>(bufSize, 0xFE);

    ThreadExecutor executor;
    AOContext aoCtx(executor);
    auto dev = NullDevice::create(aoCtx);

    Event finished;
    asyncInvoke(aoCtx, [&] {
        dev->write(etalonData, [&](auto err, auto n) {
            EXPECT_TRUE(aoCtx.workInThisThread());

            EXPECT_FALSE(err);
            EXPECT_EQ(n, bufSize);

            finished.set();
        });
    });

    finished.wait();
}

TEST(IOTest, NullDevice_Read)   // NOLINT
{
    constexpr auto bufSize = 1024;

    ThreadExecutor executor;
    AOContext aoCtx(executor);
    auto dev = NullDevice::create(aoCtx);

    Event finished;
    auto buf = std::vector<std::uint8_t>(bufSize);
    asyncInvoke(aoCtx, [&] {
        dev->read(buf, [&](auto err, auto n) {
            EXPECT_TRUE(aoCtx.workInThisThread());

            EXPECT_FALSE(err);
            EXPECT_EQ(n, 0);

            finished.set();
        });
    });

    finished.wait();
}

TEST(IOTest, BitSeqReader)   // NOLINT
{
    const auto etalonData = std::vector<std::uint8_t>{
      0b01101101,
      0b11011011,
      0b10110110,
      0b01101101,
    };

    ThreadExecutor executor;
    AOContext aoCtx(executor);

    auto dev = BitSeqReader::create(aoCtx, {true, false, true});

    std::vector<std::uint8_t> buf(4);
    Event finished;
    asyncInvoke(aoCtx, [&] {
        dev->read(buf, [&](auto err, auto n) {
            EXPECT_TRUE(aoCtx.workInThisThread());
            EXPECT_FALSE(err);
            EXPECT_EQ(buf.size(), n);

            finished.set();
        });
    });

    finished.wait();
    EXPECT_EQ(buf, etalonData);
}

TEST(IOTest, PspReader)   // NOLINT
{
    const auto etalonData = std::vector<std::uint8_t>{
      0b01101101,
      0b11011011,
      0b10110110,
      0b01101101,
    };

    ThreadExecutor executor;
    AOContext aoCtx(executor);

    auto dev = BitSeqReader::create(aoCtx, etalonData, etalonData.size() * 8);

    std::vector<std::uint8_t> buf(etalonData.size());
    Event finished;
    asyncInvoke(aoCtx, [&] {
        dev->read(buf, [&](auto, auto n) {
            EXPECT_EQ(buf.size(), n);

            finished.set();
        });
    });

    finished.wait();
    EXPECT_EQ(buf, etalonData);
}

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
    asyncInvoke(aoCtx, [&] {
        dev.read(buf, [&](const std::exception_ptr& err, std::size_t size) {
            EXPECT_TRUE(aoCtx.workInThisThread());
            EXPECT_FALSE(err);
            EXPECT_EQ(size, etalonData.size());

            const auto data = gsl::span<const std::uint8_t>(buf).first(size);
            EXPECT_EQ(data, gsl::span(etalonData));

            finished.set();
        });
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
    asyncInvoke(aoCtx, [&] {
        dev.write(etalonData, [&](const std::exception_ptr& err, std::size_t size) {
            EXPECT_TRUE(aoCtx.workInThisThread());
            EXPECT_FALSE(err);
            EXPECT_EQ(size, writeSize);

            finished.set();
        });
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

    const auto data = asyncInvoke(aoCtx, [&] {
                          return read(dev, bytesCount);
                      }).get();

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

    auto future = asyncInvoke(aoCtx, [&] {
        return read(dev, bytesCount);
    });

    EXPECT_THROW(future.get(), std::system_error);   // NOLINT
}

TEST(IOTest, CancelRead)   // NOLINT
{
    constexpr auto iterCount = 100;
    constexpr auto bufSize = 1024;
    const auto etalonData = std::vector<std::uint8_t>(bufSize);

    ThreadExecutor executor;

    for (auto i = 0; i < iterCount; ++i) {
        AOContext aoCtx(executor);
        AOContext ioCtx(aoCtx);

        std::thread([&] {
            std::this_thread::sleep_for(20ms);
            ioCtx.close();
        }).detach();

        auto dev = BitSeqReader::create(ioCtx, {false});

        while (true) {
            auto future = asyncInvoke(aoCtx, [&] {
                return read(*dev, bufSize);
            });

            try {
                EXPECT_LE(future.get(), etalonData);
            } catch (const AsyncOperationWasCancelled&) {
                break;
            } catch (...) {
                FAIL() << "Invalid exception";
                break;
            }
        }
    }
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

    const auto written = asyncInvoke(aoCtx, [&] {
                             return write(dev, data);
                         }).get();

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

    auto future = asyncInvoke(aoCtx, [&] {
        return write(dev, data);
    });

    EXPECT_THROW(future.get(), std::system_error);   // NOLINT
}

TEST(IOTest, CancelWrite)   // NOLINT
{
    constexpr auto iterCount = 100;
    constexpr auto bufSize = 1024;
    const auto data = std::vector<std::uint8_t>(bufSize, 0x3);

    ThreadExecutor executor;

    for (auto i = 0; i < iterCount; ++i) {
        AOContext aoCtx(executor);
        AOContext ioCtx(aoCtx);

        std::thread([&] {
            std::this_thread::sleep_for(2ms);
            ioCtx.close();
        }).detach();

        auto dev = NullDevice::create(ioCtx);

        while (true) {
            auto future = asyncInvoke(aoCtx, [&] {
                return write(*dev, data);
            });

            try {
                EXPECT_LE(future.get(), bufSize);
            } catch (const AsyncOperationWasCancelled&) {
                break;
            } catch (...) {
                FAIL() << "Invalid exception";
                break;
            }
        }
    }
}

TEST(IOTest, ReadExactly)   // NOLINT
{
    const auto firstPart = std::vector<std::uint8_t>(70, 0xFD);
    const auto secondPart = std::vector<std::uint8_t>(70, 0xFE);
    const auto etalonData = concatContainers(firstPart, secondPart);

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

    const auto data = asyncInvoke(aoCtx, [&] {
                          return readExactly(dev, etalonData.size());
                      }).get();
    EXPECT_EQ(data, etalonData);
}

TEST(IOTest, ReadExactlyFailed)   // NOLINT
{
    const auto firstPart = std::vector<std::uint8_t>(70, 0xFD);
    const auto secondPart = std::vector<std::uint8_t>(70, 0xFE);
    const auto etalonData = concatContainers(firstPart, secondPart);

    ThreadExecutor executor;
    AOContext aoCtx(executor);
    StubDevice dev(aoCtx, {
                            // NOLINTNEXTLINE
                            AsioStub::ReadOp{140, gsl::span(etalonData).subspan(0, 30)},
                            // NOLINTNEXTLINE
                            AsioStub::ReadOp{110, std::errc::io_error},
                            AsioStub::CloseOp{},
                          });

    auto future = asyncInvoke(aoCtx, [&] {
        return readExactly(dev, etalonData.size());
    });

    // NOLINTNEXTLINE
    EXPECT_THROW(future.get(), std::system_error);
}

TEST(IOTest, WriteExactly)   // NOLINT
{
    const auto firstPart = std::vector<std::uint8_t>(70, 0xFD);
    const auto secondPart = std::vector<std::uint8_t>(70, 0xFE);
    const auto data = concatContainers(firstPart, secondPart);

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

    auto future = asyncInvoke(aoCtx, [&] {
        return writeExactly(dev, data);
    });

    EXPECT_EQ(future.get(), data.size());
}

TEST(IOTest, WriteExactlyFailed)   // NOLINT
{
    const auto firstPart = std::vector<std::uint8_t>(70, 0xFD);
    const auto secondPart = std::vector<std::uint8_t>(70, 0xFE);
    const auto data = concatContainers(firstPart, secondPart);

    ThreadExecutor executor;
    AOContext aoCtx(executor);
    StubDevice dev(aoCtx, {
                            // NOLINTNEXTLINE
                            AsioStub::WriteOp{data, 30},
                            // NOLINTNEXTLINE
                            AsioStub::WriteOp{gsl::span(data).last(110), std::errc::io_error},
                            AsioStub::CloseOp{},
                          });

    auto future = asyncInvoke(aoCtx, [&] {
        return writeExactly(dev, data);
    });

    // NOLINTNEXTLINE
    EXPECT_THROW(future.get(), std::system_error);
}

TEST(IOTest, readUntil)   // NOLINT
{
    ThreadExecutor executor;
    AOContext aoCtx(executor);

    constexpr auto etalon = "0123456789"sv;
    StubDevice dev(aoCtx, {
                            AsioStub::ReadOp{1, "0"sv},
                            AsioStub::ReadOp{1, "1"sv},
                            AsioStub::ReadOp{1, "2"sv},
                            AsioStub::ReadOp{1, "3"sv},
                            AsioStub::ReadOp{1, "4"sv},
                            AsioStub::ReadOp{1, "5"sv},
                            AsioStub::ReadOp{1, "6"sv},
                            AsioStub::ReadOp{1, "7"sv},
                            AsioStub::ReadOp{1, "8"sv},
                            AsioStub::ReadOp{1, "9"sv},
                            AsioStub::CloseOp{},
                          });

    constexpr auto expect = "789"sv;
    const auto data = invoke(aoCtx, [&] {
        return readUntil(dev, std::vector<std::uint8_t>(expect.begin(), expect.end()));
    });
    EXPECT_EQ(data, std::vector<std::uint8_t>(etalon.begin(), etalon.end()));
}

TEST(IOTest, readLine)   // NOLINT
{
    constexpr auto etalonLines = std::array{
      "1"sv,
      "23"sv,
      ""sv,
    };

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

    for (const auto& etalonLine : etalonLines) {
        const auto line = asyncInvoke(aoCtx, [&] {
                              return readLine(dev);
                          }).get();
        EXPECT_EQ(line, etalonLine);
    }
}

TEST(IOTest, readFile)   // NOLINT
{
    ThreadExecutor executor;
    AOContext aoCtx(executor);

    const auto thisFileData = asyncInvoke(aoCtx, [&] {
                                  return File::readAll(aoCtx, __FILE__);
                              }).get();
    EXPECT_EQ(thisFileData.size(), std::filesystem::file_size(__FILE__));
}

TEST(IOTest, writeFile)   // NOLINT
{
    ThreadExecutor executor;
    AOContext aoCtx(executor);

    const auto thisFileData = asyncInvoke(aoCtx, [&] {
                                  return File::readAll(aoCtx, __FILE__);
                              }).get();

    auto dev = File::open(aoCtx, "temp-file", OpenFileMode::WriteOnly);
    auto future = asyncInvoke(aoCtx, [&] {
        return writeExactly(*dev, thisFileData);
    });
    EXPECT_EQ(future.get(), std::filesystem::file_size(__FILE__));   // NOLINT
    dev.reset();

    const auto tempFileData = asyncInvoke(aoCtx, [&] {
                                  return File::readAll(aoCtx, "temp-file");
                              }).get();
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
    static constexpr auto dataSize = 512 * 1024;
    const auto etalonData = std::vector<uint8_t>(dataSize, 1);

    test::TcpEchoServer echoServer;
    ThreadExecutor e;
    AOContext aoCtx(e);

    auto conn = TcpSocket::connect(aoCtx, test::TcpEchoServer::srvAddress, test::TcpEchoServer::srvPort).get();
    asyncInvoke(aoCtx, [&] {
        return writeExactly(*conn, etalonData)
          .then([&](const std::size_t written) {
              EXPECT_EQ(written, dataSize);
              return readExactly(*conn, dataSize);
          })
          .then([&](const std::vector<std::uint8_t>& readded) {
              EXPECT_EQ(readded, etalonData);
          });
    }).get();
}

TEST(IOTest, tcpSockAddresses)   // NOLINT
{
    test::TcpEchoServer echoServer;
    ThreadExecutor e;
    AOContext aoCtx(e);

    const auto srvAddress = std::string_view(test::TcpEchoServer::srvAddress);

    auto conn = TcpSocket::connect(aoCtx, test::TcpEchoServer::srvAddress, test::TcpEchoServer::srvPort).get();
    const auto peerAddress = conn->peerAddress().toString();
    const auto localAddress = conn->localAddress().toString();
    EXPECT_TRUE(peerAddress.substr(0, srvAddress.size()) == srvAddress);
    EXPECT_TRUE(localAddress.substr(0, srvAddress.size()) == srvAddress);
}

TEST(IOTest, tcpServerBindAddress)   // NOLINT
{
    test::TcpEchoServer echoServer;
    ThreadExecutor e;
    AOContext aoCtx(e);

    constexpr uint16_t listenPort{27777};

    auto tcp = TcpServer::start(aoCtx, {"*", listenPort});
    auto bind = tcp->bindAddress();
    const auto port = bind.port();
    EXPECT_TRUE(port.has_value());
    EXPECT_EQ(port.value(), listenPort);   // NOLINT
}

TEST(IOTest, tcpSocketAssign)   // NOLINT
{
    test::TcpEchoServer echoServer;
    ThreadExecutor e;
    AOContext aoCtx(e);

    // some legacy api socket
    sockaddr_in servaddr{};
    const auto sockfd = socket(AF_INET, SOCK_STREAM, 0);
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htonl(0x7f000001);
    servaddr.sin_port = htons(test::TcpEchoServer::srvPort);
    connect(sockfd, (sockaddr*)&servaddr, sizeof(servaddr));

    auto wrapper = TcpSocket::create(aoCtx, sockfd);
    const std::vector<std::uint8_t> data{0, 1, 2, 3, 4, 5};
    nhope::write(*wrapper, data).get();
    auto readded = nhope::read(*wrapper, data.size()).get();
    EXPECT_EQ(readded, data);
    close(sockfd);
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

TEST(IOTest, shutdownConnect)   // NOLINT
{
    ThreadExecutor e;
    AOContext aoCtx(e);

    test::TcpEchoServer echoServer;
    std::vector<std::uint8_t> buf(4);

    {
        auto conDev = TcpSocket::connect(aoCtx, test::TcpEchoServer::srvAddress, test::TcpEchoServer::srvPort).get();
        conDev->shutdown(TcpSocket::Shutdown::Receive);
        conDev->read(buf, [](const std::exception_ptr&, std::size_t c) {
            EXPECT_EQ(c, 0);
        });
        nhope::write(*conDev, {1, 2, 3}).get();
        conDev->shutdown(TcpSocket::Shutdown::Send);
        // NOLINTNEXTLINE
        EXPECT_THROW(nhope::write(*conDev, {1, 2, 3}).get(), std::runtime_error);
    }

    {
        auto dev = TcpSocket::connect(aoCtx, test::TcpEchoServer::srvAddress, test::TcpEchoServer::srvPort).get();
        dev->shutdown();
        // NOLINTNEXTLINE
        EXPECT_THROW(nhope::write(*dev, {1, 2, 3}).get(), std::runtime_error);
    }
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
    // #ifdef __linux__
    // nhope::test::VirtualSerialPort com("/dev/ttyS7", "/dev/ttyS8");
    // #endif
    const auto ports = SerialPort::availableDevices();
    for (const auto& portName : ports) {
        EXPECT_TRUE(std::filesystem::exists(portName));
    }
}

TEST(IOTest, openSerialPort)   // NOLINT
{
    ThreadExecutor e;
    AOContext aoCtx(e);

#ifdef __linux__
    SerialPortParams p{};
    {
        nhope::test::VirtualSerialPort com("/tmp/com_1", "/tmp/com_2");

        //NOLINTNEXTLINE
        EXPECT_NO_THROW(SerialPort::open(aoCtx, "/tmp/com_1", p));
    }
    p.baudrate = SerialPortParams::BaudRate::Baud1200;

    auto open = [&] {
        try {
            nhope::test::VirtualSerialPort com("/tmp/com_1", "/tmp/com_2");
            SerialPort::open(aoCtx, "/tmp/com_1", p);
        } catch (...) {
        }
    };
    auto testFlow = [&] {
        for (size_t i = 0; i < 4; ++i) {
            p.flow = static_cast<SerialPortParams::FlowControl>(i);
            open();
        }
        p = SerialPortParams{};
    };
    auto testParity = [&] {
        for (size_t i = 0; i < 4; ++i) {
            p.parity = static_cast<SerialPortParams::Parity>(i);
            open();
        }
        p = SerialPortParams{};
    };
    auto testStopBits = [&] {
        for (size_t i = 0; i < 4; ++i) {
            p.stopbits = static_cast<SerialPortParams::StopBits>(i);
            open();
        }
        p = SerialPortParams{};
    };

    testFlow();
    testParity();
    testStopBits();

#endif
}

TEST(IOTest, modemControlSerialPort)   // NOLINT
{
    ThreadExecutor e;
    AOContext aoCtx(e);

#ifdef __linux__
    SerialPortParams p{};
    {
        nhope::test::VirtualSerialPort com("/tmp/com_1", "/tmp/com_2");

        //NOLINTNEXTLINE
        EXPECT_NO_THROW(SerialPort::open(aoCtx, "/tmp/com_1", p));
    }
    p.baudrate = SerialPortParams::BaudRate::Baud1200;
    nhope::test::VirtualSerialPort com("/tmp/com_1", "/tmp/com_2");
    const auto serial = SerialPort::open(aoCtx, "/tmp/com_1", p);

    //VirtualSerialPort don't support modem control
    EXPECT_THROW(serial->getModemControl(), std::system_error);   //NOLINT
    EXPECT_THROW(serial->setRTS(true), std::system_error);        //NOLINT
    EXPECT_THROW(serial->setRTS(false), std::system_error);       //NOLINT
    EXPECT_THROW(serial->setDTR(true), std::system_error);        //NOLINT
    EXPECT_THROW(serial->setDTR(false), std::system_error);       //NOLINT
#else
    GTEST_SKIP();   //NOLINT
#endif
}

constexpr auto copyPortionSize = 4 * 1024;

TEST(IOTest, Copy)   // NOLINT
{
    ThreadExecutor executor;
    AOContext aoCtx(executor);

    // data: "123456789012345678901234567890"
    StubDevice src(aoCtx, {
                            AsioStub::ReadOp{copyPortionSize, "1234567890"sv},   // чтение 1 порции
                            AsioStub::ReadOp{copyPortionSize, "1234567890"sv},   // чтение 2 порции
                            AsioStub::ReadOp{copyPortionSize, "1234567890"sv},   // чтение 3 порции
                            AsioStub::ReadOp{copyPortionSize, ""sv},             // EOF
                            AsioStub::CloseOp{},
                          });

    StubDevice dest(aoCtx, {
                             // Запись 1 порции
                             AsioStub::WriteOp("1234567890"sv, 10),   // NOLINT

                             // Запись 2 порции
                             AsioStub::WriteOp("1234567890"sv, 5),   // NOLINT
                             AsioStub::WriteOp("67890"sv, 5),        // NOLINT

                             // Запись 3 порции
                             AsioStub::WriteOp("1234567890"sv, 1),   // NOLINT
                             AsioStub::WriteOp("234567890"sv, 1),    // NOLINT
                             AsioStub::WriteOp("34567890"sv, 1),     // NOLINT
                             AsioStub::WriteOp("4567890"sv, 1),      // NOLINT
                             AsioStub::WriteOp("567890"sv, 1),       // NOLINT
                             AsioStub::WriteOp("67890"sv, 1),        // NOLINT
                             AsioStub::WriteOp("7890"sv, 1),         // NOLINT
                             AsioStub::WriteOp("890"sv, 1),          // NOLINT
                             AsioStub::WriteOp("90"sv, 1),           // NOLINT
                             AsioStub::WriteOp("0"sv, 1),            // NOLINT

                             AsioStub::CloseOp{},
                           });

    const auto size = asyncInvoke(aoCtx, [&] {
                          return copy(src, dest);
                      }).get();

    EXPECT_EQ(size, 30);
}

TEST(IOTest, Copy2)   // NOLINT
{
    ThreadExecutor executor;
    AOContext aoCtx(executor);

    const auto n = asyncInvoke(aoCtx, [&] {
                       return copy(                                   //
                         StringReader::create(aoCtx, "1234567890"),   //
                         NullDevice::create(aoCtx));
                   }).get();

    EXPECT_EQ(n, 10);
}

TEST(IOTest, Copy_ReadFailed)   // NOLINT
{
    ThreadExecutor executor;
    AOContext aoCtx(executor);

    StubDevice src(aoCtx, {
                            AsioStub::ReadOp{copyPortionSize, "1234567890"sv},   // чтение 1 порции
                            AsioStub::ReadOp{copyPortionSize, std::errc::io_error},
                          });

    StubDevice dest(aoCtx, {
                             // Запись 1 порции
                             AsioStub::WriteOp("1234567890"sv, 10),   // NOLINT
                             AsioStub::CloseOp{},
                           });

    auto future = asyncInvoke(aoCtx, [&] {
        return copy(src, dest);
    });

    EXPECT_THROW(future.get(), std::system_error);   // NOLINT
}

TEST(IOTest, Copy_WriteFailed)   // NOLINT
{
    ThreadExecutor executor;
    AOContext aoCtx(executor);

    StubDevice src(aoCtx, {
                            AsioStub::ReadOp{copyPortionSize, "1234567890"sv},   // чтение 1 порции
                            AsioStub::CloseOp{},
                          });

    StubDevice dest(aoCtx, {
                             // Запись 1 порции
                             AsioStub::WriteOp("1234567890"sv, std::errc::io_error),   // NOLINT
                             AsioStub::CloseOp{},
                           });

    auto future = asyncInvoke(aoCtx, [&] {
        return copy(src, dest);
    });

    EXPECT_THROW(future.get(), std::system_error);   // NOLINT
}

TEST(IOTest, CancelCopy)   // NOLINT
{
    ThreadExecutor executor;
    AOContext aoCtx(executor);
    auto dest = NullDevice::create(aoCtx);
    auto src = BitSeqReader::create(aoCtx, {false});

    auto future = asyncInvoke(aoCtx, [&] {
        return copy(*src, *dest);
    });

    std::this_thread::sleep_for(100ms);

    aoCtx.close();

    EXPECT_THROW(future.get(), AsyncOperationWasCancelled);   // NOLINT
}

TEST(IOTest, StringReader)   // NOLINT
{
    constexpr auto etalonData = std::array{
      "12345"sv,
      "67890"sv,
      ""sv,
    };

    ThreadExecutor executor;
    AOContext aoCtx(executor);

    auto dev = StringReader::create(aoCtx, "1234567890");
    for (const auto etalonStr : etalonData) {
        const auto readdedStr = asyncInvoke(aoCtx, [&] {
                                    return read(*dev, etalonStr.size());
                                }).get();
        EXPECT_TRUE(eq(readdedStr, etalonStr));
    }
}

TEST(IOTest, Concat)   // NOLINT
{
    constexpr auto etalonData = "1234567890"sv;

    ThreadExecutor executor;
    AOContext aoCtx(executor);

    auto dev = concat(aoCtx,                                //
                      StringReader::create(aoCtx, "12"),    //
                      StringReader::create(aoCtx, "345"),   //
                      StringReader::create(aoCtx, "67890"));

    const auto readedData = asyncInvoke(aoCtx, [&] {
                                return readAll(*dev);
                            }).get();

    EXPECT_TRUE(eq(readedData, etalonData));
}

TEST(IOTest, Concat_Failed)   // NOLINT
{
    ThreadExecutor executor;
    AOContext aoCtx(executor);

    auto dev = concat(aoCtx, std::make_unique<StubDevice>(aoCtx, AsioStub::Operations{
                                                                   AsioStub::ReadOp(1, std::errc::io_error),
                                                                 }));

    auto future = asyncInvoke(aoCtx, [&] {
        return read(*dev, 1);
    });

    EXPECT_THROW(future.get(), std::system_error);   // NOLINT
}

TEST(IOTest, PushbackReader)   // NOLINT
{
    constexpr auto etalonData = "1234567890"sv;

    ThreadExecutor executor;
    AOContext aoCtx(executor);

    auto stringReader = StringReader::create(aoCtx, std::string(etalonData));
    auto pushbackReader = PushbackReader::create(aoCtx, std::move(stringReader));

    const auto d1 = invoke(aoCtx, [&] {
        return read(*pushbackReader, 4);
    });
    EXPECT_TRUE(eq(d1, etalonData.substr(0, 4)));

    constexpr auto part = std::array{static_cast<std::uint8_t>('1'), static_cast<std::uint8_t>('2')};

    invoke(aoCtx, [&] {
        pushbackReader->unread(std::array{static_cast<std::uint8_t>('3'), static_cast<std::uint8_t>('4')});
        pushbackReader->unread(part);
    });
    const auto res = nhope::read(*pushbackReader, 2).get();
    EXPECT_TRUE(eq(res, "12"sv));
    pushbackReader->unread(part);

    const auto d2 = invoke(aoCtx, [&] {
        return readAll(*pushbackReader);
    });
    EXPECT_TRUE(eq(d2, etalonData));
}

// http://gitlab.olimp.lan/alekseev/nhope/-/issues/28
TEST(IOTest, PushbackReader_ReadUnreadRead)   //  NOLINT
{
    constexpr auto etalonData = "1234567890"sv;

    ThreadExecutor executor;
    AOContext aoCtx(executor);

    auto stringReader = StringReader::create(aoCtx, std::string(etalonData));
    auto pushbackReader = PushbackReader::create(aoCtx, std::move(stringReader));

    const auto d1 = invoke(aoCtx, [&] {
        return read(*pushbackReader, 4);
    });

    invoke(aoCtx, [&] {
        pushbackReader->unread(gsl::span(d1).first(2));
    });

    const auto d2 = invoke(aoCtx, [&] {
        return read(*pushbackReader, 4);
    });

    // Read only onread data
    EXPECT_TRUE(eq(d2, etalonData.substr(0, 2)));
}

TEST(IOTest, PushbackReader_FailRead)   // NOLINT
{
    ThreadExecutor executor;
    AOContext aoCtx(executor);

    auto dev = std::make_unique<StubDevice>(aoCtx, AsioStub::Operations{
                                                     AsioStub::ReadOp{2, std::errc::io_error},
                                                     AsioStub::CloseOp{},
                                                   });
    auto pushbackReader = PushbackReader::create(aoCtx, *dev);
    std::array<uint8_t, 2> buf{};

    Event retrived;
    pushbackReader->read(buf, [&](const std::exception_ptr& e, std::size_t c) {
        EXPECT_EQ(c, 0);
        EXPECT_THROW(std::rethrow_exception(e), std::system_error);   // NOLINT
        retrived.set();
    });
    retrived.wait();
}

TEST(IOTest, StringWritter)   // NOLINT
{
    constexpr auto testData = std::array{
      "12345"sv,
      "67890"sv,
      ""sv,
    };

    ThreadExecutor executor;
    AOContext aoCtx(executor);

    auto dev = StringWritter::create(aoCtx);
    for (const auto str : testData) {
        asyncInvoke(aoCtx, [&] {
            // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
            const auto data = gsl::span{reinterpret_cast<const std::uint8_t*>(str.data()), str.size()};
            return write(*dev, {data.begin(), data.end()});
        }).get();
    }

    const auto contetnt = dev->takeContent();
    EXPECT_EQ(contetnt, "1234567890");
}

TEST(IOTest, AsioDeviceWrapper_toExceptionPtr)   // NOLINT
{
    using asio::error::misc_errors;
    using asio::error::misc_category;

    EXPECT_EQ(detail::toExceptionPtr(std::error_code()), nullptr);

    const auto eof = std::error_code(misc_errors::eof, misc_category);
    EXPECT_EQ(detail::toExceptionPtr(eof), nullptr);

    const auto ioErrCode = std::make_error_code(std::errc::io_error);
    EXPECT_NE(detail::toExceptionPtr(ioErrCode), nullptr);
}

TEST(IOTest, IoCancel)   // NOLINT
{
    ThreadExecutor executor;
    AOContext aoCtx(executor);
    StubDevice dev(aoCtx, AsioStub::Operations{
                            AsioStub::CancelOp{},
                            AsioStub::CloseOp{},
                          });
    EXPECT_NO_THROW((dev.ioCancel()));   // NOLINT
}

TEST(IOTest, tcpOptions)   //NOLINT
{
    const test::TcpEchoServer echoServer;
    ThreadExecutor e;
    AOContext aoCtx(e);

    auto conn = TcpSocket::connect(aoCtx, test::TcpEchoServer::srvAddress, test::TcpEchoServer::srvPort).get();
    TcpSocket::Options opts;
    opts.reuseAddress = true;
    opts.keepAlive = true;
    opts.receiveBufferSize = 2048;
    opts.sendBufferSize = 4096;
    EXPECT_NE(opts.reuseAddress, conn->options().reuseAddress);
    conn->setOptions(opts);
    EXPECT_EQ(opts.reuseAddress, conn->options().reuseAddress);
    EXPECT_EQ(opts.keepAlive, conn->options().keepAlive);
    EXPECT_EQ(opts.receiveBufferSize, conn->options().receiveBufferSize);
    EXPECT_EQ(opts.sendBufferSize, conn->options().sendBufferSize);

#ifdef __linux__
    EXPECT_TRUE(conn->nativeHandle() != -1);
#endif
}

TEST(IOTest, udpSocket)   //NOLINT
{
    ThreadExecutor e;
    AOContext aoCtx(e);

    const auto clientPort{55578};

    UdpSocket::Params p;
    p.bindAddress.address = "0.0.0.0";
    p.bindAddress.port = 0;
    p.reuseAddress = true;
    p.broadcast = true;
    p.receiveBufferSize = 2048;
    p.sendBufferSize = 4096;

    test::UdpEchoServer echoServer(p);

    UdpSocket::Params clientParams;
    clientParams.bindAddress.address = "0.0.0.0";
    clientParams.bindAddress.port = clientPort;
    clientParams.peerAddress.emplace();
    clientParams.peerAddress->port = echoServer.bindPort();
    clientParams.peerAddress->address = "127.0.0.1";

    auto client = UdpSocket::create(aoCtx, clientParams);
    const std::vector<std::uint8_t> etalon{{1, 2, 3, 4, 5, 4, 3, 2, 1}};
    const auto size = nhope::write(*client, etalon).get();
    EXPECT_EQ(size, etalon.size());

    EXPECT_EQ(*echoServer.peer().port(), clientPort);

    auto receiveData = nhope::read(*client, size).get();
    EXPECT_EQ(receiveData, etalon);
    const std::vector<std::uint8_t> etalonToAddr{{5, 4, 3, 5, 1}};
    EXPECT_EQ(
      client
        ->sendTo(etalonToAddr, UdpSocket::Endpoint{clientParams.peerAddress->address, clientParams.peerAddress->port})
        .get(),
      etalonToAddr.size());
    auto receiveAddressData = nhope::read(*client, size).get();
    EXPECT_EQ(receiveAddressData, etalonToAddr);
}

TEST(IOTest, udpSocketCancel)   //NOLINT
{
    ThreadExecutor e;
    AOContext aoCtx(e);
    auto socket = UdpSocket::create(aoCtx, {});
    auto future = asyncInvoke(aoCtx, [&] {
        auto future = nhope::read(*socket, 4);
        socket->ioCancel();
        return future;
    });
    EXPECT_THROW(future.get(), std::system_error);   // NOLINT
}

TEST(IOTest, udpSocketAssign)   // NOLINT
{
    ThreadExecutor e;
    AOContext aoCtx(e);
    UdpSocket::Params p;
    p.bindAddress.address = "0.0.0.0";
    p.bindAddress.port = 0;

    test::UdpEchoServer echoServer(p);
    const auto port = echoServer.bindPort();
    const auto sockfd = socket(AF_INET, SOCK_DGRAM, 0);

    // legacy udp socket
    sockaddr_in servaddr{};
    servaddr.sin_family = AF_INET;   // IPv4
    servaddr.sin_addr.s_addr = INADDR_ANY;
    servaddr.sin_port = htons(port);
    const std::vector<std::uint8_t> etalon{1, 2, 3, 4, 5, 4, 3, 2, 1};
    sendto(sockfd, (const char*)etalon.data(), (int)etalon.size(), 0x800, (const struct sockaddr*)&servaddr,
           sizeof(servaddr));

    auto client = UdpMultiPeerSocket::create(aoCtx, sockfd);
    client->addPeer(UdpSocket::Endpoint{"127.0.0.1", port});
    auto receiveData = invoke(aoCtx, [&] {
        return nhope::read(*client, etalon.size());
    });
    EXPECT_EQ(receiveData, etalon);

    invoke(aoCtx, [&] {
        return nhope::write(*client, etalon);
    });
    std::vector<std::uint8_t> rx(etalon.size());
    while (recv(sockfd, (char*)rx.data(), rx.size(), 0) != etalon.size()) {
    }
    EXPECT_EQ(rx, etalon);

    close(sockfd);
}

TEST(IOTest, localSocket)   // NOLINT
{
    ThreadExecutor e;
    AOContext aoCtx(e);
    auto path = std::filesystem::temp_directory_path() / "nhope-local-socket";
    const LocalServerParams params{path.string()};
    auto server = LocalServer::start(aoCtx, params);
    auto tx = LocalSocket::connect(aoCtx, params.address).get();
    const std::vector<std::uint8_t> data{1, 2, 3, 4, 5, 6};
    EXPECT_EQ(nhope::write(*tx, data).get(), data.size());
    auto rx = server->accept().get();
    EXPECT_EQ(nhope::read(*rx, data.size()).get(), data);
}

TEST(IOTest, localSocketErrors)   // NOLINT
{
    ThreadExecutor e;
    AOContext aoCtx(e);
    EXPECT_THROW(LocalSocket::connect(aoCtx, "not-exists").get(), std::system_error);

    Future<std::unique_ptr<LocalSocket>> f;
    auto path = std::filesystem::temp_directory_path() / "nhope-err-socket";
    const LocalServerParams params{path.string()};
    {
        auto server = LocalServer::start(aoCtx, params);
        f = server->accept();
    }
    EXPECT_THROW(f.get(), std::system_error);

    auto serverAoCloses = LocalServer::start(aoCtx, params);
    auto closeF = serverAoCloses->accept();
    aoCtx.close();
    EXPECT_THROW(closeF.get(), std::system_error);
}

TEST(IOTest, udpMultiSocket)   // NOLINT
{
    ThreadExecutor e;
    AOContext aoCtx(e);
    const auto clientPort{55578};

    UdpSocket::Params params;
    params.bindAddress.address = "0.0.0.0";
    params.bindAddress.port = 0;
    params.peerAddress = UdpSocket::Endpoint{"127.0.0.1", clientPort};
    UdpSocket::Endpoint secondPeer{"127.0.0.1", clientPort + 1};
    auto socket = UdpMultiPeerSocket::create(aoCtx, params);
    ASSERT_EQ(socket->peers().size(), 1);
    socket->addPeer(secondPeer);
    ASSERT_EQ(socket->peers().size(), 2);
    socket->removePeer(secondPeer);

    ASSERT_EQ(socket->peers().size(), 1);
    ASSERT_EQ(socket->peers()[0], *params.peerAddress);
    socket->removePeer(*params.peerAddress);

    const std::vector<std::uint8_t> data(56);
    EXPECT_EQ(nhope::write(*socket, data).get(), data.size());

    socket->addPeer(*params.peerAddress);
    socket->addPeer(secondPeer);
    EXPECT_EQ(nhope::write(*socket, data).get(), data.size());
}
