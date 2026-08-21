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
| Priced products | 10 (European, digital, arithmetic/geometric Asian, all 8 barrier types, both lookback styles, American, Heston stochastic volatility, plus CRR binomial and forwards) |
| Tests | 199, all passing on `debug`/`release`/`ubsan` locally and the full CI matrix |

![Thread scaling](docs/benchmarks/phase4-scaling.svg)

Live-regenerated versions of this chart and the rest of the numbers above run
from `python tools/generate_report.py` — see the "Phase 6 — generated
results" section of `docs/validation-report.md`.

## Live demo

Deployed on AWS (Lambda container image, ARM64/Graviton, API Gateway HTTP API,
S3 + CloudFront) — see `docs/design/07-aws-demo.md` and the Phase 7 section of
`docs/validation-report.md` for the full architecture and cost-guardrail rationale.

| Metric | Value |
|---|---|
| Frontend | https://da9f58rzd0wm1.cloudfront.net |
| API | https://4cpy3vq7l8.execute-api.us-east-2.amazonaws.com |
| Cold-start latency (measured, 2 runs) | 1.22s, 0.42s (mean 0.82s) |
| Warm-request latency (measured, 8 runs) | 0.15–0.29s (mean 0.20s) |
| Estimated cost at zero traffic | ~$0.02/month (entirely ECR container-image storage: ~179 MiB × $0.10/GiB-month; Lambda, API Gateway, CloudFront, and S3 are all effectively $0 with no requests) |

Every marquee chart on the live site (thread scaling, false-sharing A/B) is
served from committed JSON (`docs/benchmarks/*.json`), never recomputed on
request — a Lambda invocation cannot reproduce a controlled multi-core scaling
measurement. Live pricing, the convergence explorer, the variance-reduction
comparison, the Greeks surface, and the CFA invariant table all call the real
deployed engine.

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
├── core/      Philox4x32-10 RNG (+ NEON-vectorised batch path), inverse
│              normal CDF, Welford accumulator, thread pool, Householder
│              QR, hand-written JSON, timing, hand-verified Sobol
│              sequence, from-scratch Gauss-Legendre quadrature
├── models/    GBM exact log-Euler simulation, Brownian-bridge path
│              construction, Heston stochastic volatility
├── payoffs/   Payoff / PathPayoff concepts (European, Asian, barrier,
│              lookback, digital)
├── pricers/   Analytic oracles, CRR binomial, Monte Carlo, Longstaff-Schwartz,
│              Sobol QMC, Heston (QE Monte Carlo + semi-analytic)
└── greeks/    Finite-difference Greeks with common random numbers,
             likelihood-ratio Greeks, pathwise-derivative Greeks

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
  was this engine's weakest estimate under finite differences — fixed by a
  likelihood-ratio estimator (`mcd::greeks::likelihood_ratio_*`, Stretch
  Goal 1): measured 834× lower gamma standard error than FD for an
  at-the-money digital, 11× for a barrier near its knock level. FD Greeks
  remain the default for products LR doesn't cover (Asian, lookback,
  American) and for European/digital/barrier callers who haven't switched.
- **American Greeks require a frozen exercise boundary.** Bumping spot and
  refitting the LSM regression moves the fitted exercise boundary
  discontinuously, producing extremely noisy deltas. Mitigated by freezing
  the base run's fitted policy and repricing bumped scenarios against it —
  quantified (not just asserted) in `docs/validation-report.md` Phase 5.
- **Pathwise Greeks have no gamma at all, and are silently wrong for
  discontinuous payoffs** (Stretch Goal 2). Pathwise differentiates the
  payoff itself, not the density — a payoff's kink differentiated twice is
  a Dirac delta (no gamma exists for any product under this method), and a
  discontinuous payoff's naive pathwise delta measurably converges to
  exactly `0.0` regardless of path count (`pathwise_digital_delta_naive_
  and_broken`, deliberately not exposed as a product feature — see the
  validation report for the measured failure). Where it *is* valid
  (European, Asian), it's the cheapest and lowest-variance of the three
  Greek estimators in this project — measured 2.6× lower delta standard
  error than likelihood-ratio and 18× lower than finite-difference at
  matched parameters.
- **Sobol QMC is capped at 7 dimensions, by disclosed design, not
  oversight** (Stretch Goal 3). No published direction-number table
  (Joe-Kuo or similar) is used — this implementation only trusts what it
  independently verified this session: 6 primitive polynomials over GF(2),
  confirmed by direct LFSR maximal-period simulation, plus the simplest
  legal initial direction numbers. Correctness-verified, not
  discrepancy-optimal — real measured convergence still beats plain MC's
  −0.5 log-log slope (measured −0.7469 vs. −0.5643 at matched path
  counts), but higher-dimension products (barriers, lookbacks, LSM) are
  out of scope for this pass.
- **SIMD targets ARM NEON, not AVX2/AVX-512** (Stretch Goal 4) — a
  deliberate choice, not a compromise: this dev machine and the deployed
  AWS Lambda are both ARM64, so NEON is verifiable and directly usable in
  production, unlike an x86 path that would only ever run in a container.
  The win is real but modest (1.31× full-pipeline throughput, 1.08× on
  the RNG primitive alone) because the inverse CDF's transcendental calls
  (`log`/`exp`/`erfc`) have no portable NEON intrinsic guaranteed
  bit-identical to the scalar path, so only the integer Philox step and
  the polynomial evaluation are truly vectorised — reported as measured,
  not rounded up to a nicer number.
- **Heston is European-only, no exotics under stochastic volatility**
  (Stretch Goal 5). QE's discretization bias is real (unlike GBM's exact
  simulation) — measured and reported at 3 step counts in the validation
  report rather than assumed negligible. The characteristic-function
  pricer's numerical conditioning degrades for `xi` pushed too close to
  zero (division by `xi²`) — documented with a real measured floor, not
  just tested away.
