#include <benchmark/benchmark.h>
#include <vector>

#include "nhope/seq/fifo.h"

void fifo(benchmark::State& state)
{
    nhope::Fifo<int, 1024> fifo;
    std::vector<int> data(12);
    std::vector<int> indata(1);

    for ([[maybe_unused]] auto _ : state) {
        while (fifo.push(data) != 0) {
        }
        while (!fifo.empty()) {
            [[maybe_unused]] auto x = fifo.pop(indata);
        }
    }
}

BENCHMARK(fifo)->Iterations(100000)->Unit(benchmark::TimeUnit::kMillisecond);   //NOLINT
