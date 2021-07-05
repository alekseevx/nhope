#include "nhope/io/file.h"
#include "nhope/async/thread-executor.h"
#include "nhope/io/io-device.h"

#include <array>
#include <benchmark/benchmark.h>
#include <cstdint>

namespace {

constexpr auto bufSize{4096};

}   // namespace

void fileReader(benchmark::State& state)
{
    nhope::ThreadExecutor e;
    nhope::FileSettings s;
    s.fileName = "/dev/urandom";
    s.mode = nhope::FileMode::ReadOnly;

    auto dev = openFile(e, s);

    for (auto _ : state) {
        nhope::readExactly(*dev, bufSize).get();
    }
}

void fileWriter(benchmark::State& state)
{
    nhope::ThreadExecutor e;
    nhope::FileSettings s;
    std::vector<uint8_t> buffer(bufSize);

    s.fileName = "/tmp/stub";
    s.mode = nhope::FileMode::WriteOnly;

    auto dev = openFile(e, s);

    for (auto _ : state) {
        nhope::writeExactly(*dev, buffer).get();
    }
}

BENCHMARK(fileReader)->Iterations(100000)->Unit(benchmark::TimeUnit::kMillisecond);   //NOLINT
BENCHMARK(fileWriter)->Iterations(100000)->Unit(benchmark::TimeUnit::kMillisecond);   //NOLINT

