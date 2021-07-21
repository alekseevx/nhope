#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <memory>
#include <thread>
#include <vector>

#include <asio/buffer.hpp>
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
#include "nhope/io/network.h"
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
        using namespace asio::ip;

        try {
            asio::io_context ctx;
            tcp::socket sock(ctx);
            tcp::endpoint endpoint(address_v4::from_string("127.0.0.1"), port);
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
    Session(nhope::Executor& executor, std::unique_ptr<nhope::IoDevice> client)
      : m_aoCtx(executor)
      , m_client(std::move(client))
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
        m_client->read(receiveBufSize).then(m_aoCtx, [this](auto buf) {
            this->m_receivedBytes += buf.size();

            if (this->m_receivedBytes >= sizeOfTransmittedDataPerIteration) {
                this->m_finishPromise.setValue(m_receivedBytes);
                m_selfAnchor.reset();
                return;
            }

            this->startRead();
        });
    }

private:
    std::shared_ptr<Session> m_selfAnchor;

    std::unique_ptr<nhope::IoDevice> m_client;
    std::uint64_t m_receivedBytes = 0;

    nhope::Promise<std::uint64_t> m_finishPromise;
    nhope::AOContext m_aoCtx;
};

void doNextIteration(benchmark::State& state, std::uint64_t& receivedBytes)
{
    state.PauseTiming();
    asio::io_context ioCtx(1);
    auto workGuard = asio::make_work_guard(ioCtx);
    nhope::IOContextSequenceExecutor executor(ioCtx);
    nhope::AOContext aoCtx(executor);

    auto srv = nhope::listen(executor, {nhope::Endpoint{port, "127.0.0.1"}});
    startSend();

    srv->accept()
      .then(aoCtx,
            [&](auto client) {
                state.ResumeTiming();
                auto session = std::make_shared<Session>(executor, std::move(client));

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
    for (auto _ : state) {
        doNextIteration(state, receivedBytes);
    }
    state.SetBytesProcessed(static_cast<std::int64_t>(receivedBytes));
}

}   // namespace

BENCHMARK(tcpReader)   // NOLINT
  ->Iterations(iterCount)
  ->Unit(benchmark::TimeUnit::kMillisecond);
