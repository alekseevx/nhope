#include <cstdint>
#include "nhope/async/ao-context.h"
#include "nhope/async/async-invoke.h"
#include "nhope/async/thread-executor.h"
#include <benchmark/benchmark.h>

namespace {

constexpr std::int64_t callInvokeCount = 10'000;
constexpr benchmark::IterationCount iterCount = 100;

void doInvokeIteration(nhope::AOContext& aoCtx, std::uint64_t num)
{
    while (--num > 0) {
        nhope::invoke(aoCtx, [] {});
    }
}

void invoke(benchmark::State& state)
{
    nhope::ThreadExecutor executor;
    nhope::AOContext aoCtx(executor);

    for (auto _ : state) {
        doInvokeIteration(aoCtx, state.range());
    }
}

}   // namespace

BENCHMARK(invoke)   // NOLINT
  ->Arg(callInvokeCount)
  ->Iterations(iterCount)
  ->Unit(benchmark::TimeUnit::kMillisecond);
