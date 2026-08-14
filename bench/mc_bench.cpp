#include "mcd/core/stats.hpp"
#include "mcd/pricers/monte_carlo.hpp"

#include <benchmark/benchmark.h>

#include <thread>
#include <vector>

namespace {

// --- Phase 2 baseline: single-threaded paths/second ----------------------------------

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

// --- Phase 4: paths/second and ns/path vs. thread count -------------------------------

void BM_MonteCarloEuropeanThreads(benchmark::State& state) {
    const auto num_threads = static_cast<unsigned>(state.range(0));
    constexpr std::uint64_t kPathCount = 20'000'000;
    std::uint64_t seed = 0;
    for (auto _ : state) {
        auto result = mcd::pricers::monte_carlo_european(
            100.0, 100.0, 0.05, 0.01, 0.25, 1.0, mcd::OptionType::Call, kPathCount, seed,
            {.num_threads = num_threads});
        benchmark::DoNotOptimize(result);
        ++seed;
    }
    state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations()) *
                             static_cast<std::int64_t>(kPathCount));
    state.counters["threads"] = static_cast<double>(num_threads);
}

BENCHMARK(BM_MonteCarloEuropeanThreads)
    ->DenseRange(1, static_cast<int>(2 * std::thread::hardware_concurrency()), 1)
    ->Unit(benchmark::kMillisecond)
    ->UseRealTime();

// --- Phase 4: false-sharing A/B ---------------------------------------------------
// Every thread repeatedly updates its own slot in an accumulator array. Unpadded,
// plain WelfordAccumulator elements are small enough that several adjacent threads'
// slots share a cache line, causing genuine cross-core invalidation traffic; padded
// PaddedWelford elements each occupy their own line. Both benchmarks do identical
// work (same iteration count, same thread count) -- only the layout differs.

constexpr int kFalseSharingIterationsPerThread = 2'000'000;

void BM_FalseSharingUnpadded(benchmark::State& state) {
    const unsigned n = std::thread::hardware_concurrency();
    for (auto _ : state) {
        std::vector<mcd::WelfordAccumulator> accumulators(n);
        std::vector<std::jthread> threads;
        threads.reserve(n);
        for (unsigned i = 0; i < n; ++i) {
            threads.emplace_back([&accumulators, i] {
                for (int k = 0; k < kFalseSharingIterationsPerThread; ++k) {
                    accumulators[i].add(static_cast<double>(k));
                }
            });
        }
    }
}
BENCHMARK(BM_FalseSharingUnpadded)->Unit(benchmark::kMillisecond)->UseRealTime();

void BM_FalseSharingPadded(benchmark::State& state) {
    const unsigned n = std::thread::hardware_concurrency();
    for (auto _ : state) {
        std::vector<mcd::PaddedWelford> accumulators(n);
        std::vector<std::jthread> threads;
        threads.reserve(n);
        for (unsigned i = 0; i < n; ++i) {
            threads.emplace_back([&accumulators, i] {
                for (int k = 0; k < kFalseSharingIterationsPerThread; ++k) {
                    accumulators[i].acc.add(static_cast<double>(k));
                }
            });
        }
    }
}
BENCHMARK(BM_FalseSharingPadded)->Unit(benchmark::kMillisecond)->UseRealTime();

} // namespace

BENCHMARK_MAIN();
