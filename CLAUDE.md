# CLAUDE.md — Monte Carlo Derivatives Pricing & Risk Engine

You are the sole engineer on this project. This document is your specification,
your roadmap, and your contract. Execute it in order. Do not skip phases. Do not
reorder phases. Stop at every gate and report before continuing.

---

## 1. Mission

Build `mcd` — a C++20 Monte Carlo derivatives pricing and risk engine with:

- Closed-form analytic pricers used as validation oracles
- A multithreaded Monte Carlo core with **bitwise-reproducible** results
  independent of thread count
- Path-dependent exotics: Asian, barrier, lookback, digital
- American options via Longstaff–Schwartz
- Finite-difference Greeks with common random numbers
- A benchmark suite reporting paths/second and parallel scaling efficiency
- Python bindings and an AWS-hosted demo web application

The project doubles as a CFA Level I Derivatives study artifact: relationships the
curriculum teaches (cost of carry, put–call parity, risk-neutral binomial
valuation) are encoded as executable tests, and the engine extends into the
continuous-time machinery the curriculum stops short of.

**The primary purpose of this repository is to demonstrate C++ systems
engineering, concurrency, and numerical rigor.** When a decision trades polish in
any other dimension against those three, choose those three.

---

## 2. Non-negotiable constraints

Violating any of these is a defect regardless of whether tests pass.

1. **Never use `-ffast-math`, `-Ofast`, or `-funsafe-math-optimizations.`** They
   break IEEE semantics, NaN handling, and reproducibility. The README must
   contain a short section explaining this rejection.
2. **Never fabricate a benchmark number.** Every figure in any document, README,
   report, or commit message must come from a benchmark you actually executed in
   this session. If you cannot run it, write `TBD` and say so.
3. **Never copy text, tables, exhibits, or problem sets from the CFA
   curriculum into this repository.** The underlying mathematics is public domain
   and may be implemented and described freely in your own words. The curriculum's
   prose and exhibits are copyrighted. Reference modules by number and topic only
   (e.g. "LM9 — put–call parity"). Never paste curriculum text.
4. **Never add a third-party dependency beyond those listed in §5.** If you
   believe one is needed, stop and ask.
5. **Never implement a numerical method you cannot test against an independent
   reference.** Every pricer needs an oracle: a closed form, an alternative
   method, or a limiting case.
6. **Never mark a phase complete with a failing, skipped, or disabled test.**
7. **Never commit generated build artifacts, benchmark binaries, or `.vscode/`.**

---

## 3. Working method

This project uses **spec-driven development**. For each phase:

1. Write `docs/design/NN-<phase>.md` first: requirements, interfaces, algorithms,
   test plan, acceptance criteria. **Stop and present it. Wait for approval.**
2. Write the tests before the implementation.
3. Implement until tests pass.
4. Run the benchmarks defined for that phase and record real numbers.
5. Update `docs/validation-report.md`.
6. **Stop at the phase gate. Report results against every acceptance criterion
   explicitly, one by one, with actual measured values. Wait for approval before
   Phase N+1.**

Commit discipline: conventional commits (`feat:`, `fix:`, `test:`, `perf:`,
`docs:`, `refactor:`, `build:`). One logical change per commit. Every commit must
build and pass tests.

When you hit a genuine fork not settled by this document, **stop and ask**. Do not
guess and do not silently pick. Specifically: any change to the RNG scheme, the
threading model, the determinism guarantee, the public API shape, or the AWS cost
profile requires approval.

---

## 4. Locked design decisions

These are settled. Do not relitigate them; implement them.

### Language and toolchain

| Decision | Value |
|---|---|
| Standard | C++20 (concepts, `std::span`, `std::jthread`, `<numbers>`, `<bit>`) |
| Compilers | GCC 13+ and Clang 16+ must both build cleanly |
| Warnings | `-Wall -Wextra -Wpedantic -Wshadow -Wconversion`, warnings-as-errors in CI |
| Release flags | `-O3 -march=native -DNDEBUG` |
| Build system | CMake ≥ 3.25 with `CMakePresets.json` |
| Dependency fetch | CMake `FetchContent` only |
| Test framework | GoogleTest (typed and value-parameterised tests for the product matrix) |
| Benchmark framework | Google Benchmark for micro-benchmarks; custom harness for scaling sweeps |
| Formatting | `.clang-format` (LLVM base, 100 cols), `.clang-tidy` enforced in CI |

### Numerics and RNG

| Decision | Value | Rationale to preserve in docs |
|---|---|---|
| RNG | **Philox 4×32-10**, counter-based | Path *i* draws the same numbers regardless of thread count or scheduling. This is what makes bitwise determinism possible. Implement it yourself; it is ~60 lines |
| Normal transform | **Inverse CDF** (Acklam's rational approximation, refined by one Halley step) | Preserves the counter↔path mapping; required if Sobol QMC is added later |
| Accumulation | **Welford's online algorithm**, per-thread | One-pass stable mean and variance; gives the standard error for free |
| Reduction | Pairwise merge of per-thread Welford accumulators | Deterministic merge order — merge by thread index, never by completion order |
| Path storage | **Streaming**, fixed-size stack buffer | Zero heap allocation in the hot loop. Exception: Longstaff–Schwartz, which must store paths — document this explicitly as a deliberate deviation |
| Precision | `double` throughout | Do not offer a `float` mode |

### Concurrency

| Decision | Value |
|---|---|
| Model | Hand-written thread pool over `std::jthread`. **Do not use OpenMP, `std::async`, or `std::execution::par_unseq`** |
| Work division | Static contiguous chunking of the path index space |
| Accumulators | One per thread, padded to `std::hardware_destructive_interference_size` |
| Determinism guarantee | **Prices must be bitwise identical for 1 thread and N threads, for every product, for every path count.** This is a hard acceptance criterion, not an aspiration |
| Thread count | Defaults to `std::thread::hardware_concurrency()`, overridable via CLI |

### Products and models

- **Model:** Geometric Brownian Motion with constant volatility, continuous
  dividend/carry yield `q`. Only this model is in scope.
- **Products in scope:** European call/put; arithmetic-average Asian (fixed and
  floating strike); geometric-average Asian; all eight barrier types (up/down ×
  in/out × call/put); lookback (fixed and floating strike); digital
  (cash-or-nothing, asset-or-nothing); American call/put via Longstaff–Schwartz.
- **Variance reduction:** antithetic variates; control variates (geometric Asian
  as the control for arithmetic Asian); Brownian-bridge continuity correction for
  discretely-monitored barriers.
- **Greeks:** finite differences only, with the constraints in §6 Phase 5.

### Testing philosophy

Monte Carlo output is a random variable. **Never assert a fixed epsilon against a
Monte Carlo price.** The rule is:

```
ASSERT(|price_mc − price_analytic| < 3.0 * standard_error_mc)
```

Every such test must use a fixed RNG seed so failures are reproducible, and must
report both the deviation and the standard error in its failure message. A test
that passes with a fixed epsilon is a test that will flake or that is testing
nothing.

---

## 5. Permitted dependencies

Nothing else without approval.

- GoogleTest (test only)
- Google Benchmark (bench only)
- pybind11 (Phase 6)
- Standard library

The RNG, the inverse normal CDF, the linear algebra for Longstaff–Schwartz
regression, the thread pool, and every pricer are hand-written. That is the point.

---

## 6. Roadmap

### Phase 0 — Specification and scaffold

Deliverables: `docs/design/00-requirements.md`; repo skeleton; CMake with presets
(`debug`, `release`, `asan`, `ubsan`, `tsan`); GoogleTest and Google Benchmark
wired via FetchContent; `.clang-format`, `.clang-tidy`, `.gitignore`; GitHub
Actions workflow (matrix: {GCC, Clang} × {Debug, Release}, plus ASan/UBSan/TSan
jobs); a trivial passing test.

Layout:

```
mcd/
├── CMakeLists.txt  CMakePresets.json  CLAUDE.md  README.md
├── docs/{design/,validation-report.md,cfa-mapping.md,benchmarks/}
├── include/mcd/
│   ├── core/      types.hpp rng.hpp normal.hpp stats.hpp thread_pool.hpp
│   ├── models/    gbm.hpp
│   ├── payoffs/   european.hpp asian.hpp barrier.hpp lookback.hpp digital.hpp
│   ├── pricers/   analytic.hpp binomial.hpp monte_carlo.hpp lsm.hpp
│   └── greeks/    finite_difference.hpp
├── src/  apps/mcd_cli/  tests/  bench/  bindings/python/  web/  infra/
```

**Gate:** CI green on all six matrix jobs.

---

### Phase 1 — Analytic layer and CFA invariants

Implement, with unit tests against published reference values:

- Black–Scholes–Merton European call/put with carry yield `q`
- Black-76 (options on futures)
- Kemna–Vorst geometric-average Asian
- Reiner–Rubinstein continuous barriers, all eight types
- Goldman–Sosin–Gatto lookbacks
- Digital (cash-or-nothing, asset-or-nothing)
- Forward price under cost of carry; forward contract value during its life
- Futures price at inception and mark-to-market value
- Cox–Ross–Rubinstein binomial, one-period and *n*-period, with explicit
  risk-neutral probability

Then write `tests/cfa_invariants_test.cpp` and `docs/cfa-mapping.md`. Each
invariant below is a test; the mapping document states which Level I Derivatives
learning module it corresponds to, in your own words.

| Invariant | Assertion | Module |
|---|---|---|
| Cost of carry | `F₀ = S₀·e^((r−q)T)` | LM4 |
| Forward value at initiation is zero | `V₀ = 0` at the fair forward price | LM5 |
| Forward value during life | `Vₜ = (Fₜ − F₀)·e^(−r(T−t))` | LM5 |
| Futures/forward equivalence | equal under deterministic rates | LM6 |
| Exercise value + time value | `option value = exercise value + time value`, both non-negative before expiry | LM8 |
| Six factor sensitivities | sign of each numerical sensitivity matches the curriculum's directional claim (`∂C/∂S > 0`, `∂C/∂σ > 0`, `∂C/∂X < 0`, `∂P/∂X > 0`, etc.) | LM8 |
| Put–call parity | `C + Xe^(−rT) = P + S₀e^(−qT)` to 1e-12 | LM9 |
| Put–call forward parity | `C + Xe^(−rT) = P + F₀e^(−rT)` to 1e-12 | LM9 |
| Binomial risk-neutrality | `π = (e^((r−q)Δt) − d)/(u − d)`, and price is independent of the real-world drift | LM10 |
| Binomial → BSM | CRR price converges to BSM as *n* → ∞; assert error < 0.01 at n = 5000 and that error decreases monotonically over n ∈ {10, 50, 250, 1000, 5000} | LM10 |
| No-arbitrage bounds | `max(S₀e^(−qT) − Xe^(−rT), 0) ≤ C ≤ S₀e^(−qT)` | LM4, LM8 |
| Monotonicity | call value non-decreasing in S and σ, non-increasing in X, across a swept grid | LM8 |

**Gate:** all analytic tests pass; all invariant tests pass; `docs/cfa-mapping.md`
written; no curriculum text copied.

---

### Phase 2 — Single-threaded Monte Carlo core

Implement Philox 4×32-10, the inverse normal CDF, the Welford accumulator, the GBM
path generator (streaming, exact log-Euler solution — not an Euler discretisation
of the SDE), and a payoff concept. Price European options and validate against
Phase 1's BSM.

Required tests:

- Philox reproduces the published Random123 test vectors
- Inverse CDF: max absolute error < 1e-9 across u ∈ (1e-12, 1−1e-12)
- Generated normals pass moment tests (mean, variance, skew, excess kurtosis) and
  a Kolmogorov–Smirnov test at n = 10⁶
- Welford matches a naive two-pass mean/variance to 1e-12 on 10⁶ samples
- European MC within 3 SE of BSM for a matrix of ≥ 20 (S, X, σ, T, r, q) combinations
- **Determinism:** identical seed ⇒ bitwise-identical price across repeated runs
- **Allocation:** zero heap allocations inside the pricing loop, proven by
  overriding global `operator new` in a test fixture and asserting the counter is
  unchanged across 10⁶ paths

Benchmark: single-threaded paths/second for a European option at 10⁶ paths.

**Gate:** all of the above; record baseline paths/sec in
`docs/benchmarks/phase2.md`.

---

### Phase 3 — Exotics and variance reduction

Implement Asian (arithmetic and geometric, fixed and floating strike), barriers
(all eight, with a `monitoring_frequency` parameter), lookbacks, and digitals.
Then antithetic variates, the geometric-Asian control variate for arithmetic
Asian, and the Brownian-bridge barrier correction.

Required tests:

- Geometric Asian within 3 SE of Kemna–Vorst
- Barrier within 3 SE of Reiner–Rubinstein as monitoring frequency → continuous
  (assert convergence over ≥ 4 monitoring frequencies)
- Lookback within 3 SE of Goldman–Sosin–Gatto
- In–out parity: `knock_in + knock_out == vanilla` to within combined SE, for all
  four in/out pairs
- Digital call ≤ vanilla call spread bound
- Asian value ≤ corresponding European value (averaging reduces volatility)
- Antithetic reduces variance for every product; report the factor
- Control variate reduces arithmetic-Asian variance by ≥ 5×; report the factor
- Brownian bridge reduces discrete-monitoring bias; report before/after bias
  against the continuous closed form

Documentation: a table of measured variance-reduction factors per product, and a
log–log RMSE-vs-paths convergence plot whose fitted slope is asserted to be
−0.5 ± 0.05.

**Gate:** all above; `docs/validation-report.md` populated with the convergence
plot and the VR table.

---

### Phase 4 — Parallelism

Implement the thread pool, static chunking, padded per-thread accumulators, and
deterministic pairwise reduction.

Required tests:

- **Bitwise determinism across thread counts:** for every product, for thread
  counts {1, 2, 4, 8, hardware_concurrency}, prices must compare bitwise equal.
  Use `std::bit_cast<uint64_t>` and assert integer equality — not
  `EXPECT_DOUBLE_EQ`
- TSan clean under the full test suite
- ASan and UBSan clean
- Pool correctness: no lost work, no double execution, correct behaviour at
  path counts smaller than the thread count

Benchmarks and required artifacts:

- Paths/second and ns/path vs. thread count, 1 → 2× hardware_concurrency
- Speedup curve and parallel efficiency; fit and report Amdahl's serial fraction
- **False-sharing A/B:** benchmark the padded accumulator against a deliberately
  unpadded variant and report the measured difference. This is evidence, not
  folklore — do not claim a benefit you have not measured
- Every benchmark record must include CPU model, core count, compiler and version,
  and flags. State explicitly whether turbo/boost was active and that results are
  the median of ≥ 5 runs

**Gate:** bitwise determinism test green; TSan clean; parallel efficiency ≥ 80% at
physical core count, or a written analysis of why not.

---

### Phase 5 — Greeks and American options

**Finite-difference Greeks.** Delta, gamma, vega, theta, rho. Central differences.

Two requirements are mandatory, not optional:

1. **Common random numbers.** The base and bumped runs must consume the identical
   random stream. With Philox this is automatic if the counter is keyed on path
   index and not on call order — verify it explicitly with a test.
2. **Bump-size analysis.** Sweep the bump size over ≥ 6 orders of magnitude and
   plot the error, showing the truncation-error and Monte-Carlo-noise regimes and
   the optimum between them. Document the chosen default and why. Gamma is the
   binding constraint; expect roughly `h ∝ ε^(1/4)`-scale behaviour and show it.

Tests: FD Greeks within 3 SE of BSM analytic Greeks for European options.

**Longstaff–Schwartz American pricing.** Backward induction with a regression on
in-the-money paths. Basis: Laguerre polynomials to degree 3 by default, with the
basis pluggable. Hand-write the least-squares solve — use QR via Householder
reflections, not the normal equations, which are numerically poor.

Tests and required properties:

- American call on a non-dividend-paying stock equals the European call (early
  exercise is never optimal) — this is the sharpest available correctness check
- American put ≥ European put, and ≥ its immediate exercise value everywhere
- LSM price within 3 SE of a fine binomial-tree American price across a matrix of
  parameters
- Convergence in both path count and basis degree, reported
- The known low bias of LSM must be documented, and the price reported as a
  lower-bound estimator

**Greeks on American options — read carefully.** Bumping spot re-fits the
regression, so the exercise boundary moves discontinuously with the bump and the
resulting delta is extremely noisy. You must:

- Use common random numbers across base and bumped runs
- **Freeze the exercise boundary from the base run** and re-price the bumped
  scenario against that frozen policy
- Document both mitigations and quantify the improvement by benchmarking against
  the naive re-fit approach

Note in `docs/validation-report.md` that gamma for discontinuous payoffs (digitals,
barriers near the boundary) remains the weakest estimate this engine produces, and
that a likelihood-ratio estimator is the correct fix. This is a known, documented
limitation — not a hidden one.

**Gate:** all above; bump-size study plotted; American-equals-European call test
green.

---

### Phase 6 — CLI, Python bindings, and reporting

- `mcd_cli`: JSON in, JSON out. Every priced result carries the point estimate,
  the standard error, a 95% confidence interval, the path count, the seed, the
  elapsed time, and the paths/second achieved. **A price reported without a
  confidence interval is an incomplete result** — enforce this in the output schema.
- pybind11 bindings exposing pricers, Greeks, and the benchmark harness. Release
  the GIL around pricing calls.
- A report generator producing `docs/validation-report.md` with all convergence
  plots, the VR table, the scaling curve, the bump-size study, and the CFA
  invariant results table.
- README with a results table in the first screenful.

**Gate:** `pip install -e .` works; a Python smoke test prices every product;
report regenerates from scratch via one command.

---

### Phase 7 — AWS demo web application

Strictly after the engine is complete. This layer exists to **display the
engine's evidence**, not to be a generic pricing form.

Required content, in priority order:

1. Interactive convergence explorer — price and CI band vs. path count, live
2. Thread-scaling chart and the false-sharing A/B, served from **precomputed
   committed JSON** (`docs/benchmarks/*.json`), never computed on request
3. Variance-reduction comparison — antithetic and control variate on/off
4. Live pricing across all products, always showing the confidence interval
5. Greeks surfaces over a spot/time grid
6. The CFA invariant table, rendered live and green

Architecture:

| Component | Choice |
|---|---|
| Compute | Lambda **container image**, ARM64/Graviton, scale-to-zero |
| Engine access | pybind11 module inside the image, thin Python handler |
| API | API Gateway HTTP API, throttled |
| Frontend | React + TypeScript + Vite, S3 + CloudFront |
| Charts | Recharts |
| IaC | AWS CDK in TypeScript, in `infra/` |

Cost guardrails — all mandatory:

- Hard cap on paths per request (default 5×10⁶); reject above it with a clear error
- Lambda timeout ≤ 30 s, memory tuned by measurement not by guess
- API Gateway throttling: burst 10, rate 5 req/s
- All marquee benchmark numbers served from committed JSON
- A CDK-provisioned AWS Budgets alarm at a low monthly threshold
- CloudFront caching on all GET responses
- **No authentication, no database, no VPC, no NAT Gateway.** A NAT Gateway alone
  would cost more than everything else combined

**Gate:** deployed, URL live, cold-start latency measured and reported, estimated
monthly cost at zero traffic stated in the README.

---

## 7. Stretch goals

Do not start any of these until Phase 7 is complete and approved.

1. **Likelihood-ratio Greeks** — fixes gamma for digitals and barriers. Highest
   value of anything here
2. **Pathwise-derivative Greeks** — and a written comparison of FD vs. pathwise
   vs. LR: variance, bias, cost, and where each breaks
3. **Sobol QMC with Brownian-bridge construction** — should beat the −0.5
   convergence slope; prove it on the same log–log plot
4. **SIMD** — vectorised Philox and inverse CDF, with honest before/after numbers
5. **Heston stochastic volatility** — QE scheme, validated against the
   semi-analytic characteristic-function price

---

## 8. README requirements

The README is the artifact a reviewer reads first. It must open with:

1. One sentence on what this is
2. A results table: peak paths/second with hardware, parallel efficiency, max
   validation error in units of standard error, product count, test count
3. The scaling chart
4. Build and run in three commands

Then: architecture overview, design-decision rationale (Philox and determinism,
thread pool over OpenMP, streaming paths, the `-ffast-math` rejection, statistical
test tolerances), the validation methodology, the CFA mapping, and the documented
limitations from §6 Phase 5.

Write it for a reader who will spend ninety seconds. Lead with evidence.

---

## 9. Start here

Read this document fully. Then produce `docs/design/00-requirements.md` for Phase
0 and stop for approval. Do not write implementation code before that document is
approved.
