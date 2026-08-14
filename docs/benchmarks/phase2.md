# Phase 2 Benchmark — Single-Threaded Monte Carlo Core

Every number below was measured in this session by actually running
`build/release/bench/mcd_bench`. Nothing here is estimated or fabricated; if a
number couldn't be measured it would say `TBD` per CLAUDE.md §2.2.

## Machine and build configuration

| | |
|---|---|
| CPU | Apple M3 Pro (11 cores: 5 performance + 6 efficiency) |
| OS | macOS (Darwin 25.5.0), arm64 |
| Compiler | Apple clang 17.0.0 |
| Build | `release` preset — `-O3 -march=native -DNDEBUG` |
| Threads | 1 (single-threaded; Phase 4 adds the thread pool) |
| Boost/turbo state | Not explicitly controlled — this is a laptop-class dev
  machine, not a dedicated benchmarking box. Load average during the run was
  ~4.5 (other processes active), so treat this as a representative dev-machine
  baseline, not a controlled/isolated measurement. Phase 4's scaling study
  will need a properly controlled environment; noting that gap explicitly now
  rather than presenting this as more rigorous than it is. |

## Result

Google Benchmark, 7 repetitions, median and standard deviation reported
(`--benchmark_repetitions=7 --benchmark_report_aggregates_only=true`), pricing
a European call at 10⁶ paths per iteration:

| Metric | Value |
|---|---|
| Median wall time | 64.8 ms |
| Mean wall time | 64.9 ms |
| Std. dev | 0.294 ms (CV 0.45%) |
| **Paths/second** | **~15.4 million/sec** |

This covers: Philox4x32-10 draw, Acklam inverse-CDF transform (+ Halley
refinement), GBM terminal-spot computation, payoff evaluation, discounting,
and Welford accumulation — one full path of end-to-end work per unit, with
zero heap allocation (verified separately in
`MonteCarloEuropean.ZeroHeapAllocationsInPricingLoop`).

## Reproduce

```
cmake --preset release
cmake --build --preset release
./build/release/bench/mcd_bench --benchmark_repetitions=7 --benchmark_report_aggregates_only=true
```
