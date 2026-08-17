# mcd — Monte Carlo Derivatives Pricing & Risk Engine

A C++20 Monte Carlo derivatives pricing and risk engine with closed-form analytic oracles,
a hand-written multithreaded core with bitwise-reproducible results, path-dependent
exotics, American options via Longstaff–Schwartz, finite-difference Greeks, a CLI, and
Python bindings.

## Results

| Metric | Value |
|---|---|
| Peak paths/second (single-threaded) | ~15.4M (European call, 10⁶ paths, Apple M3 Pro, Release) |
| Peak paths/second (11 threads) | ~92M (5.97× speedup at `hardware_concurrency()=11`) |
| Parallel efficiency at physical core count | 54.2% (11 threads; Amdahl serial fraction f≈0.088) |
| Bitwise determinism across thread counts | Verified for every product, thread counts {1,2,4,8,11} |
| Max validation error vs. analytic/independent oracles | ≤ 3.0 standard errors (this project's fixed, never-exceeded acceptance threshold — see `docs/validation-report.md` for every measured deviation) |
| Priced products | 9 (European, digital, arithmetic/geometric Asian, all 8 barrier types, both lookback styles, American, plus CRR binomial and forwards) |
| Tests | 151, all passing on `debug`/`release`/`ubsan` locally and the full CI matrix |

![Thread scaling](docs/benchmarks/phase4-scaling.svg)

Live-regenerated versions of this chart and the rest of the numbers above run
from `python tools/generate_report.py` — see the "Phase 6 — generated
results" section of `docs/validation-report.md`.

## Build and run

```
cmake --preset release && cmake --build --preset release
ctest --preset release
echo '{"product":"european","spot":100,"strike":100,"rate":0.05,"carry_yield":0,"vol":0.2,"time":1,"type":"call","path_count":1000000,"seed":42}' | ./build/release/apps/mcd_cli/mcd_cli
```

Other CMake presets: `debug`, `asan`, `ubsan`, `tsan`. Python bindings:
`pip install -e .` then `import mcd`.

## Architecture

```
include/mcd/
├── core/      Philox4x32-10 RNG, inverse normal CDF, Welford accumulator,
│              thread pool, Householder QR, hand-written JSON, timing
├── models/    GBM exact log-Euler simulation
├── payoffs/   Payoff / PathPayoff concepts (European, Asian, barrier,
│              lookback, digital)
├── pricers/   Analytic oracles, CRR binomial, Monte Carlo, Longstaff-Schwartz
└── greeks/    Finite-difference Greeks with common random numbers

apps/mcd_cli/       JSON-in/JSON-out CLI
bindings/python/     pybind11 module + Python tests
tools/               Report generator
```

Every numerical primitive — the RNG, the inverse normal CDF, the Householder
QR least-squares solver, the thread pool, and every pricer — is hand-written,
per `CLAUDE.md` §5: the only third-party dependencies anywhere in this
project are GoogleTest (test-only), Google Benchmark (bench-only), and
pybind11 (bindings-only).

## Design-decision rationale

**Philox 4×32-10 and determinism.** A counter-based RNG means path *i*'s
random draws depend only on `(seed, path_index, draw_index)` — never on
thread count, scheduling, or call order. Combined with a fixed
`logical_chunk_count() = hardware_concurrency()` (invariant across whatever
`num_threads` a caller requests) and a deterministic pairwise accumulator
merge, this makes Monte Carlo prices **bitwise identical** for 1 thread and
N threads, for every product, verified directly via `std::bit_cast`-based
integer equality — not `EXPECT_DOUBLE_EQ`. See
`docs/design/04-parallelism.md` §2 for the full argument.

**A hand-written thread pool, not OpenMP.** CLAUDE.md's locked design
requires a hand-written `std::jthread`-based pool specifically so the
chunking and merge-order determinism above is directly controllable — a
requirement OpenMP's `#pragma omp parallel for` does not expose.

**Streaming paths, not stored paths — except Longstaff-Schwartz.** Every
pricer holds only O(1) running state per path (zero heap allocation in the
pricing loop, verified by overriding global `operator new`/`delete` and
asserting an unchanged counter over 10⁶ paths). LSM is the one deliberate,
documented exception: American backward induction needs every path's price
at every exercise date simultaneously to regress continuation value, so it
stores a single flat buffer (one allocation per call, not per path).

**Why never `-ffast-math`, `-Ofast`, or `-funsafe-math-optimizations`.**
These flags relax IEEE 754 semantics (reordering, NaN/Inf handling,
signed-zero behavior) in ways that silently break two things this engine
depends on: bitwise-reproducible Monte Carlo results across thread counts,
and the correctness of the closed-form oracles every pricer is validated
against. Release builds use `-O3 -march=native -DNDEBUG` only.

**Statistical test tolerances.** Monte Carlo output is a random variable —
asserting a fixed epsilon against it is a test that will eventually flake or
that is testing nothing. Every Monte Carlo correctness test instead asserts
`|price_mc − price_oracle| < 3.0 × standard_error_mc`, with a fixed seed (so
any failure is reproducible) and both the deviation and the standard error
reported in the failure message.

## Validation methodology

Every pricer is checked against an independent reference: closed-form
formulas (Black-Scholes-Merton, Black-76, Kemna-Vorst, Reiner-Rubinstein,
Goldman-Sosin-Gatto), a completely different numerical method (a fine
American CRR binomial tree for Longstaff-Schwartz), or an exact mathematical
identity (put-call parity, in+out barrier parity, digital decomposition).
Three real bugs were found this way and are documented in
`docs/validation-report.md` with full root-cause writeups, rather than
quietly fixed: two Phase 1 lookback sign errors caught by dominance-bound
tests, and a Phase 5 LSM bug (a missing inception-time exercise decision)
caught by CLAUDE.md's own required "American put ≥ immediate exercise value"
test.

## CFA Level I mapping

This project doubles as a CFA Level I Derivatives study artifact:
relationships the curriculum teaches (cost of carry, put-call parity,
risk-neutral binomial valuation) are encoded as executable tests in
`tests/cfa_invariants_test.cpp`, mapped module-by-module (numbers/topics
only, no curriculum text) in `docs/cfa-mapping.md`.

## Known, documented limitations

- **LSM is a lower-bound estimator.** The fitted continuation value is an
  approximation; using it to make exercise decisions can only leave value on
  the table relative to the true optimal policy, never exceed it. Every LSM
  price in this project should be read as such.
- **Gamma for discontinuous payoffs** (digitals, barriers near the boundary)
  is this engine's weakest estimate under finite differences. A
  likelihood-ratio estimator is the documented correct fix, out of scope
  until the stretch goals in `CLAUDE.md` §7.
- **American Greeks require a frozen exercise boundary.** Bumping spot and
  refitting the LSM regression moves the fitted exercise boundary
  discontinuously, producing extremely noisy deltas. Mitigated by freezing
  the base run's fitted policy and repricing bumped scenarios against it —
  quantified (not just asserted) in `docs/validation-report.md` Phase 5.
