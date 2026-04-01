#include <benchmark/benchmark.h>

static void BM_decoder_placeholder(benchmark::State& state) {
    for (auto _ : state) {
        benchmark::DoNotOptimize(0);
    }
}

BENCHMARK(BM_decoder_placeholder);

