#include <cstdint>
#include <iostream>

#include <asio/io_context.hpp>
#include <benchmark/benchmark.h>

#include "nhope/async/ao-context.h"
#include "nhope/async/future.h"
#include "nhope/async/io-context-executor.h"

namespace {

constexpr std::int64_t callFutureThenCount = 100'000;
constexpr benchmark::IterationCount iterCount = 100;

void stop(nhope::AOContext& aoCtx)
{
    aoCtx.executor().ioCtx().stop();
}

void callFutureThen(nhope::AOContext& aoCtx, std::uint64_t num)
{
    if (--num == 0) {
        stop(aoCtx);
        return;
    }

    nhope::makeReadyFuture().then(aoCtx, [&aoCtx, num]() {
        callFutureThen(aoCtx, num);
    });
}

void doFutureThenIteration(benchmark::State& state, std::uint64_t num)
{
    state.PauseTiming();
    asio::io_context ioCtx(1);
    auto workGuard = asio::make_work_guard(ioCtx);
    nhope::IOContextSequenceExecutor executor(ioCtx);
    nhope::AOContext aoCtx(executor);
    state.ResumeTiming();

    callFutureThen(aoCtx, num);

    ioCtx.run();
}

void futureThen(benchmark::State& state)
{
    for (auto _ : state) {
        doFutureThenIteration(state, state.range());
    }
}

}   // namespace

BENCHMARK(futureThen)   // NOLINT
  ->Arg(callFutureThenCount)
  ->Iterations(iterCount)
  ->Unit(benchmark::TimeUnit::kMillisecond);
