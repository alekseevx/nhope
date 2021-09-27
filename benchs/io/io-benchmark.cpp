#include <cstdint>

#include "nhope/async/ao-context.h"
#include "nhope/io/file.h"
#include "nhope/async/thread-executor.h"
#include "nhope/io/io-device.h"

#include <benchmark/benchmark.h>

namespace {

constexpr auto bufSize{4096};

}   // namespace

void fileReader(benchmark::State& state)
{
    nhope::ThreadExecutor e;
    nhope::AOContext aoCtx(e);

    auto file = openFile(aoCtx, "/dev/urandom", nhope::OpenFileMode::ReadOnly);

    for ([[maybe_unused]] auto _ : state) {
        nhope::readExactly(*file, bufSize).get();
    }
}

void fileWriter(benchmark::State& state)
{
    nhope::ThreadExecutor e;
    nhope::AOContext aoCtx(e);

    std::vector<uint8_t> buffer(bufSize);

    auto file = openFile(aoCtx, "/dev/null", nhope::OpenFileMode::WriteOnly);

    for ([[maybe_unused]] auto _ : state) {
        nhope::writeExactly(*file, buffer).get();
    }
}

BENCHMARK(fileReader)->Iterations(100000)->Unit(benchmark::TimeUnit::kMillisecond);   //NOLINT
BENCHMARK(fileWriter)->Iterations(100000)->Unit(benchmark::TimeUnit::kMillisecond);   //NOLINT
