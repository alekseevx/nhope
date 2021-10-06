#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <memory>
#include <system_error>
#include <thread>
#include <vector>

#include <asio/buffer.hpp>
#include <asio/connect.hpp>
#include <asio/error_code.hpp>
#include <asio/io_context.hpp>
#include <asio/ip/address_v4.hpp>
#include <asio/ip/tcp.hpp>

#include <benchmark/benchmark.h>

#include "nhope/async/ao-context.h"
#include "nhope/async/executor.h"
#include "nhope/async/future.h"
#include "nhope/async/io-context-executor.h"
#include "nhope/io/io-device.h"
#include "nhope/io/tcp.h"

namespace {

constexpr benchmark::IterationCount iterCount = 10;
constexpr std::uint16_t port = 5555;
constexpr std::size_t sendBufSize = 64 * 1024;
constexpr std::size_t receiveBufSize = 1024;
constexpr std::uint64_t sizeOfTransmittedDataPerIteration = 1UL << 30;

void startSend()
{
    std::thread([] {
        using asio::ip::tcp;
        using asio::ip::address_v4;

        try {
            asio::io_context ctx;
            tcp::socket sock(ctx);
            tcp::endpoint endpoint(address_v4::loopback(), port);
            sock.connect(endpoint);

            std::vector<char> buf(sendBufSize);
            asio::error_code err;
            while (!err) {
                sock.write_some(asio::buffer(buf), err);
            }
        } catch (const std::exception& ex) {
            std::cerr << "Failed to send data:" << ex.what() << std::endl;
            std::exit(EXIT_FAILURE);
        }
    }).detach();
}

class Session final : public std::enable_shared_from_this<Session>
{
public:
    explicit Session(std::unique_ptr<nhope::IODevice> client)
      : m_client(std::move(client))
      , m_receiveBuf(receiveBufSize)
    {}

    nhope::Future<std::uint64_t> start()
    {
        auto future = m_finishPromise.future();
        this->startRead();
        m_selfAnchor = shared_from_this();
        return future;
    }

    void startRead()
    {
        m_client->read(m_receiveBuf, [this](const auto& /*err*/, auto size) {
            this->m_receivedBytes += size;

            if (this->m_receivedBytes >= sizeOfTransmittedDataPerIteration) {
                this->m_finishPromise.setValue(m_receivedBytes);
                m_selfAnchor.reset();
                return;
            }

            this->startRead();
        });

        // nhope::read(*m_client, receiveBufSize).then([this](auto data) {
        //     this->m_receivedBytes += data.size();

        //     if (this->m_receivedBytes >= sizeOfTransmittedDataPerIteration) {
        //         this->m_finishPromise.setValue(m_receivedBytes);
        //         m_selfAnchor.reset();
        //         return;
        //     }

        //     this->startRead();
        // });
    }

private:
    std::shared_ptr<Session> m_selfAnchor;

    std::unique_ptr<nhope::IODevice> m_client;
    std::vector<std::uint8_t> m_receiveBuf;
    std::uint64_t m_receivedBytes = 0;

    nhope::Promise<std::uint64_t> m_finishPromise;
};

void doNextIteration(benchmark::State& state, std::uint64_t& receivedBytes)
{
    state.PauseTiming();
    asio::io_context ioCtx(1);
    auto workGuard = asio::make_work_guard(ioCtx);
    nhope::IOContextSequenceExecutor executor(ioCtx);
    nhope::AOContext aoCtx(executor);

    auto srv = nhope::TcpServer::start(aoCtx, {"127.0.0.1", port});
    startSend();

    srv->accept()
      .then(aoCtx,
            [&](auto client) {
                auto session = std::make_shared<Session>(std::move(client));
                state.ResumeTiming();

                return session->start();
            })
      .then(aoCtx, [&](auto sessionReceivedBytes) {
          receivedBytes += sessionReceivedBytes;
          ioCtx.stop();
      });

    ioCtx.run();
}

void tcpReader(benchmark::State& state)
{
    std::uint64_t receivedBytes = 0;
    for ([[maybe_unused]] auto _ : state) {
        doNextIteration(state, receivedBytes);
    }
    state.SetBytesProcessed(static_cast<std::int64_t>(receivedBytes));
}

}   // namespace

BENCHMARK(tcpReader)   // NOLINT
  ->Iterations(iterCount)
  ->Unit(benchmark::TimeUnit::kMillisecond);

// Benchmark for comparison with ASIO
#if 1
namespace {
class AsioSession final : public std::enable_shared_from_this<AsioSession>
{
public:
    AsioSession(asio::io_context& ioCtx, asio::ip::tcp::socket client)
      : m_ioCtx(ioCtx)
      , m_client(std::move(client))
      , m_receiveBuf(receiveBufSize)
    {}

    void start()
    {
        this->startRead();
        m_selfAnchor = shared_from_this();
    }

    void startRead()
    {
        auto asioBuf = asio::buffer(m_receiveBuf);
        m_client.async_read_some(asioBuf, [this](const std::error_code& err, std::size_t n) {
            if (err) {
                throw std::system_error(err);
            }

            this->m_receivedBytes += n;
            if (this->m_receivedBytes >= sizeOfTransmittedDataPerIteration) {
                m_ioCtx.stop();
                m_selfAnchor.reset();
                return;
            }

            this->startRead();
        });
    }

    std::uint64_t receivedBytes() const
    {
        return m_receivedBytes;
    }

private:
    std::shared_ptr<AsioSession> m_selfAnchor;

    asio::io_context& m_ioCtx;

    asio::ip::tcp::socket m_client;
    std::vector<std::uint8_t> m_receiveBuf;
    std::uint64_t m_receivedBytes = 0;
};

void doNextAsioIteration(benchmark::State& state, std::uint64_t& receivedBytes)
{
    using asio::ip::tcp;
    using asio::ip::address_v4;

    state.PauseTiming();
    asio::io_context ioCtx(1);
    auto workGuard = asio::make_work_guard(ioCtx);

    tcp::acceptor acceptor(ioCtx, tcp::endpoint(address_v4::loopback(), port));

    startSend();

    auto sock = acceptor.accept();
    sock.set_option(tcp::no_delay());
    state.ResumeTiming();

    auto session = std::make_shared<AsioSession>(ioCtx, std::move(sock));
    session->startRead();

    ioCtx.run();

    receivedBytes += session->receivedBytes();
}

void tcpAsioReader(benchmark::State& state)
{
    std::uint64_t receivedBytes = 0;
    for ([[maybe_unused]] auto _ : state) {
        doNextAsioIteration(state, receivedBytes);
    }
    state.SetBytesProcessed(static_cast<std::int64_t>(receivedBytes));
}

}   // namespace

BENCHMARK(tcpAsioReader)   // NOLINT
  ->Iterations(iterCount)
  ->Unit(benchmark::TimeUnit::kMillisecond);
#endif   // Benchmark for comparison with ASIO
