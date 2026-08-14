#include "mcd/pricers/monte_carlo.hpp"

#include <benchmark/benchmark.h>

namespace {

void BM_MonteCarloEuropean(benchmark::State& state) {
    const auto path_count = static_cast<std::uint64_t>(state.range(0));
    std::uint64_t seed = 0;
    for (auto _ : state) {
        auto result = mcd::pricers::monte_carlo_european(100.0, 100.0, 0.05, 0.01, 0.25, 1.0,
                                                           mcd::OptionType::Call, path_count, seed);
        benchmark::DoNotOptimize(result);
        ++seed;
    }
    state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations()) *
                             static_cast<std::int64_t>(path_count));
}

BENCHMARK(BM_MonteCarloEuropean)->Arg(1'000'000)->Unit(benchmark::kMillisecond);

} // namespace

BENCHMARK_MAIN();
