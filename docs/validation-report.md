# Validation Report

This report is generated incrementally as each phase completes. Every number in
it must come from a benchmark or test actually executed in this repository —
never fabricated or estimated. Sections below are populated starting Phase 1
(analytic validation) and Phase 3 (variance reduction, convergence plots).

## Status

- Phase 0 (scaffold): complete, no numerical content.
- Phase 1 (analytic layer and CFA invariants): complete. 38/38 tests passing
  (debug, release, ubsan presets, and full CI matrix).
- Phase 2 (single-threaded Monte Carlo core): complete. 75/75 tests passing
  (debug, release, ubsan presets, and full CI matrix). Baseline: ~15.4M
  paths/sec single-threaded (see `docs/benchmarks/phase2.md`).
- Phase 3 (exotics and variance reduction): complete. 94/94 tests passing
  (release; debug/ubsan verification recorded below). VR factor table and
  convergence plot below.
- Phase 4 (parallelism): complete. 109/109 tests passing (debug, release,
  ubsan; tsan via CI). Bitwise determinism verified across thread counts
  1/2/4/8/hardware_concurrency() for every product. Scaling and false-sharing
  results below.
- Phase 5 (Greeks and American options): complete. 129/129 tests passing
  (debug, release, ubsan presets, all locally verified this phase). A real
  LSM bug (missing inception-time exercise decision) found by CLAUDE.md's own
  required "American put ≥ immediate exercise value" test and fixed. Bump-size
  study, QR standalone verification, and frozen-vs-naive Greeks quantification
  below.
- Phase 6 (CLI, bindings, reporting): complete. 151/151 C++ tests passing
  (debug, release, ubsan presets, locally verified this phase). Python
  bindings built and smoke-tested (`pip install -e .`); a real segfault
  (building a `py::dict` while the GIL was held released by `call_guard`)
  found and fixed. `tools/generate_report.py` verified end-to-end: it
  regenerates a delimited block of `docs/validation-report.md` from a live
  run, including an independent reproduction of Phase 5's exact bump-size
  finding. See the "Phase 6 — generated results" section below for the
  live-computed numbers and `docs/design/06-cli-bindings-reporting.md` for
  the two forks (hand-written JSON; delimited generated block) resolved
  before implementation.
- Phase 7 (AWS demo): not started.

## Phase 1 — analytic layer

**Test oracle strategy** (see `docs/design/01-analytic-layer.md` §6 for the full
rationale, approved before implementation): put-call parity, put-call forward
parity, Black-76-to-BSM reduction via the forward, and CRR-binomial-to-BSM
convergence are exact mathematical identities, not memorized numbers. Barrier
in+out=vanilla parity (all 4 direction/type combinations) and digital
decomposition (vanilla = asset-or-nothing − K·cash-or-nothing) are likewise
exact identities. Lookback options are validated by pathwise-exact dominance
bounds (a lookback must be worth at least the corresponding vanilla, since its
payoff dominates pathwise) plus a branch-continuity check at strike = spot and
a monotonicity-in-time check, rather than magic reference numbers. One
classic, widely-republished textbook example (Hull, S=42/K=40/r=10%/σ=20%/
T=0.5/q=0) is included as a loose-tolerance sanity check, not a primary oracle.

**Real bugs found and fixed via this process** (i.e., the oracle strategy did
its job): the fixed-strike lookback put formula had a sign error that produced
a negative price, caught by the dominance-bound test; the floating-strike
lookback call and put formulas had matching sign errors in their correction
term, caught the same way. All three were corrected and re-verified against
the dominance bounds, the branch-continuity check, and the monotonicity check.

**A finding, not a bug**: `CfaInvariant.ExerciseValuePlusTimeValueIsNonNegative`
initially failed for deep in-the-money European puts (e.g. S=80, K=110). This
is not an implementation defect — it is a genuine property of European options
(unlike American ones) that CLAUDE.md's invariant table states as an
unqualified rule but which the CFA Level I curriculum's introductory heuristic
does not capture precisely. Verified correct via put-call parity and
no-arbitrage bounds in the same suite. Documented in `docs/cfa-mapping.md`;
the test grid was narrowed to near-the-money strikes where the heuristic holds.

**Results**: 38/38 tests passing across `debug`, `release`, and `ubsan`
presets locally, and the full 8-job CI matrix (7 build/test jobs +
clang-tidy).

## Phase 2 — single-threaded Monte Carlo core

**Sourcing, not memory**: Philox4x32-10 constants and test vectors were
fetched and cross-verified against the Random123 reference repository
(`github.com/DEShawResearch/random123`, `tests/kat_vectors`) with two
independent `curl` fetches, not recalled. Acklam's inverse-normal-CDF
coefficients and algorithm structure (including the exact Halley refinement
step) were fetched from QuantLib's `InverseCumulativeNormal` and independently
re-verified against the raw source. All three Random123 known-answer vectors
(all-zeros, all-ones, "pi digits") match this implementation's output exactly
— strong evidence the RNG itself is bit-for-bit correct, not just
plausible-looking.

**A finding, not a bug**: the inverse-CDF accuracy test initially failed at
the extreme tail (`u = 1 - 1e-12`) with an error of ~6.4e-6, far above the
spec's 1e-9 bound. Root cause, verified empirically (see
`tests/rng_test.cpp`): a plain `double` representing `u` this close to 1 only
retains ~12 bits (~4 decimal digits) of "distance from 1" information, because
doubles near 1.0 have an absolute ULP of ~1.1e-16 against a gap of 1e-12 —
the precision is lost the moment such a `u` is stored as a double, before any
inversion algorithm sees it. QuantLib's own `InverseCumulativeNormal(double)`
has the identical limitation for the identical reason; this is not specific
to Acklam's method or this implementation. Verified the achieved error stays
under 1e-9 for `u` in `[1e-9, 1-1e-9]`, and that the growth toward the extreme
corners tracks the theoretical `machine_epsilon / phi(z)` amplification
predicted by floating-point error analysis, rather than being erratic. Tests
now assert the literal 1e-9 bound over the achievable range and a
theoretically-justified bound at the extreme corner.

**Results**: 75/75 tests passing across `debug`, `release`, and `ubsan`
presets locally. Zero heap allocations verified over 10⁶ paths (global
`operator new`/`delete` override with an atomic counter). Determinism verified
via `std::bit_cast`-based bitwise equality across repeated runs with the same
seed. European MC price within 3 standard errors of BSM across 22 parameter
combinations. See `docs/benchmarks/phase2.md` for the paths/second baseline.

## Phase 3 — exotics and variance reduction

**Architecture**: a new `PathPayoff` concept (`observe(price)` / `result()`)
drives Asian, barrier, and lookback pricing generically, holding only O(1)
running scalars per path — never the path itself — per
`docs/design/03-exotics-variance-reduction.md` §2. Digitals reuse Phase 2's
terminal-only `Payoff` concept unchanged. `monte_carlo_european`'s Phase 2
public signature is untouched; variance-reduction options are added via a
new overload and a shared internal single-step helper.

**A real bug found and fixed, via the exact process this project is built
around**: the Phase 1 floating-strike and fixed-strike lookback analytic
formulas were wrong — not just imprecise. Phase 3's independent Monte Carlo
pricer (built from first principles, with its own payoff struct verified
correct against a hand-computed deterministic path) converged to a stable,
tight-standard-error price that disagreed with the Phase 1 analytic values
by 15-30+ standard errors, far beyond anything explainable by discretization
bias. Root-caused and fixed by fetching QuantLib's
`AnalyticContinuousFixedLookbackEngine` /
`AnalyticContinuousFloatingLookbackEngine` and transliterating that verified
structure directly — deliberately *not* re-deriving by pattern-matching
against the call formula again, since that's exactly what produced the
original Phase 1 error and then reproduced a variant of it on the first
re-derivation attempt this session too. After the fix, MC and analytic
converge together as monitoring frequency increases (see below), which a
wrong formula could not do.

**A related, genuine finding (not a bug)**: geometric Asian and lookback
Monte Carlo prices carry a small, real discretization bias against their
*continuous*-monitoring closed forms (Kemna-Vorst, the lookback formulas
above) — structurally the identical phenomenon CLAUDE.md already frames as a
convergence claim for barriers ("as monitoring frequency → continuous").
Verified empirically for lookback: an 8× increase in monitoring points (500
→ 4000) shrank the bias by 2.87×, against a 2.83× prediction from the
theoretical O(1/√monitoring_points) rate — a near-exact fit. `ExoticsMc`'s
lookback tests were written as convergence tests (bias strictly decreasing
over 4 monitoring-point values), matching how the barrier test is already
structured, rather than a fixed-monitoring 3-SE match that the math doesn't
support. Geometric Asian's bias is much smaller (extremum-tracking payoffs
are far more sensitive to discrete monitoring than running-average payoffs)
and passes a direct 3-SE test at a moderately fine monitoring grid.

### Variance-reduction factor table

All figures measured this session (`tests/variance_reduction_test.cpp`,
`--gtest_filter=VarianceReduction.*`, printed `[VR]` lines), matched-cost
comparisons per `docs/design/03-exotics-variance-reduction.md` §5.

| Product | Technique | Factor |
|---|---|---|
| European | Antithetic | 3.36× |
| Arithmetic Asian | Antithetic | 3.54× |
| Barrier | Antithetic | 3.26× |
| Lookback | Antithetic | 5.20× |
| Digital | Antithetic | 258.73× |
| Arithmetic Asian | Control variate (geometric Asian, coeff=1) | 355.84× (required ≥ 5×) |
| Down-and-out barrier, 12 monitoring points | Brownian-bridge correction | bias 0.618 → 0.013 (47.6× reduction) |

Digital's very large antithetic factor is expected: its payoff is close to a
step function, so mirrored draws land on the same side of the strike far
more often than a smoothly-varying payoff would, making the antithetic pair
highly negatively correlated. The control-variate factor is likewise
expected to be large for this specific pairing — arithmetic and geometric
Asian payoffs on the same path are extremely highly correlated, which is
exactly why CLAUDE.md's locked design chose this control in the first place.

### Convergence: log-log standard error vs. path count

![Convergence plot](benchmarks/phase3-convergence.svg)

Fitted slope: **-0.5065** (required: -0.5 ± 0.05), least-squares over 7 path
counts from 10³ to 10⁶, European call, seed=2024. Raw data in
`tests/convergence_test.cpp` (`Convergence.LogLogSlopeIsMinusOneHalf`).

## Phase 4 — parallelism

**The determinism math, resolved before implementation** (full argument in
`docs/design/04-parallelism.md` §2, approved before writing any code): naively,
"one accumulator per thread, merged pairwise" (CLAUDE.md's literal wording)
and "bitwise identical for 1 thread and N threads" (CLAUDE.md's literal hard
acceptance criterion) are in tension, because Welford's closed-form merge
formula performs a genuinely different sequence of floating-point operations
than sequential accumulation — not bit-identical to it in general, even
though both are mathematically correct. The resolution: fix
`logical_chunk_count() = hardware_concurrency()`, invariant across every
call's `num_threads` value. "1 thread" means one worker sequentially
processing all `hardware_concurrency()` chunks (same fixed chunk boundaries,
same fixed chunk-index merge order as any other thread count), not "no
chunking." This makes the sequence of floating-point operations that produces
the final price completely independent of how many OS threads execute it —
only wall-clock parallelism changes. Verified directly:
`tests/parallelism_test.cpp`'s `BitwiseDeterminism.*` tests assert
`std::bit_cast<uint64_t>` equality across thread counts
{1, 2, 4, 8, hardware_concurrency()} for every product (European, digital,
arithmetic/geometric Asian with and without the control variate, barrier with
and without the Brownian-bridge correction, both lookback styles) — all pass.

**A real deadlock found and fixed during implementation, isolated with a
minimal standalone reproduction before touching any pricer code**: the first
end-to-end run of the new parallel tests hung indefinitely. Diagnosed by
writing a ~15-line reproduction of just the thread pool (no pricing logic at
all) and confirming even *zero-round* construct-then-destruct hung — isolating
the bug to shutdown, not the round/chunking logic (which a second repro
confirmed completes correctly across multiple thread counts). Root cause: on
this session's local toolchain (an unusually recent Homebrew Clang, version
22.1.8 — far beyond any known official LLVM release, almost certainly a
near-trunk development build with rough edges in newer library facilities),
`std::condition_variable_any::wait(lock, stop_token, predicate)` was not
waking threads on `request_stop()`, hanging `std::jthread`'s automatic join on
destruction. Fixed by replacing that stop-token-integrated wait with a manual
`std::atomic<bool>` flag plus a plain `std::condition_variable` — simpler,
more portable, and not dependent on that specific, newer library integration.
`std::jthread` itself is still very much in use, exactly per CLAUDE.md's
locked design; only the shutdown-signaling mechanism changed. Re-verified
fixed via the same standalone repro, then confirmed via the full test suite.

**A related toolchain finding**: Google Benchmark's own header failed to
compile under that same Clang build (`__COUNTER__` arithmetic rejected as a
"C2y extension" under `-pedantic-errors`) — unrelated to this project's code,
a third-party dependency tripping on an unusually new, strict pre-release
compiler. Benchmarks and the full local verification below instead used
Homebrew GCC 16 (a properly released, stable toolchain, and notably one of
the two compiler families CI itself uses), which also surfaced a second
genuine, independent finding: GCC warns
(`-Werror=interference-size`) that `std::hardware_destructive_interference_size`
is tuning-dependent and unsafe to bake into layout — concretely, GCC reported
256 bytes for this exact machine's tuning against libc++'s generic 64. Fixed
by using a fixed, explicitly-chosen 128-byte constant instead of reading the
standard library value at all, sidestepping the instability entirely rather
than picking a side. Both findings are documented in code comments at their
respective sites (`thread_pool.hpp`, `stats.hpp`).

**Local sanitizer coverage**: UBSan ran clean locally, including every new
concurrency test, under both the Clang-22 and GCC-16 toolchains. ASan was
previously blocked on this Mac under Apple clang (documented in
`docs/design/00-requirements.md` §6a since Phase 0); discovered during this
phase that Homebrew GCC 16's ASan runtime does not have that problem on the
same machine, giving real local ASan coverage for the first time — **108/109
tests pass, 1 correctly skipped, zero memory errors detected** (see the CI
push-back finding immediately below for what that one skip is and why).
GCC on macOS ARM64 has no working TSan runtime at all (a missing
`___tsan_init` symbol at link time — a genuine platform gap, not a bug), so
TSan correctness remains CI-only, exactly why CLAUDE.md put it there.

**A second real bug, caught only by actually pushing to CI** — worth stating
plainly: this one slipped past every local check this whole project, because
the specific failure mode (ASan/TSan's own runtime already defining
`operator new`/`operator delete`) can only manifest when a sanitizer
runtime is actually present at link time, and no sanitizer had linked
successfully on this Mac before this phase. Phase 2's
`ZeroHeapAllocationsInPricingLoop` test overrides global `operator new`/
`delete` to count allocations — a technique that is fundamentally
incompatible with ASan/TSan, whose runtimes (`libclang_rt.{asan,tsan}_cxx.a`)
already define those exact symbols for their own instrumentation. Linking
both is a hard "multiple definition" error, not a warning. CI's tsan job
failed on exactly this; asan failed identically for the same reason. Fixed
with a sanitizer-detection preprocessor guard
(`__SANITIZE_ADDRESS__`/`__SANITIZE_THREAD__`/`__has_feature`) that skips —
visibly, as `SKIPPED` in test output, with the reason stated, never silently
— only that one test's allocator-override mechanism under those two
sanitizers; debug, release, and ubsan all still run the real check. Verified
by building under GCC 16's ASan locally: links cleanly, the test reports
`SKIPPED` with its reason as designed, and the rest of the suite (108 tests)
passes with zero memory errors detected.

This is a direct, concrete illustration of exactly the gap CLAUDE.md's CI
matrix exists to close: a defect invisible to every local configuration this
project could exercise for three phases, caught the moment the real CI
environment linked a sanitizer runtime for the first time.

**A third real bug, also caught only by CI**: the `clang-tidy` job failed too,
for a reason that had likely been silently true since Phase 1. The CI step
passed header files (`include/mcd/*.hpp`) directly on clang-tidy's command
line alongside `.cpp` files. Header files have no entry of their own in
`compile_commands.json` — only actual compiled translation units do — so when
clang-tidy analyzes one standalone, it has no real compile command to infer
flags from, and falls back to a flagless invocation that can't find this
project's own `include/` directory (`'mcd/core/types.hpp' file not found`).
Fixed the standard, correct way: pass only `.cpp` files (which do have real
compile commands) and rely on `.clang-tidy`'s existing `HeaderFilterRegex` to
still surface findings in any header transitively included by one — every
header in this project is reachable that way, so nothing is lost. Verified
locally with a properly toolchain-matched clang-tidy run: zero compiler
errors, only warnings, two of which were legitimate and fixed directly
(`std::numbers::sqrt3` instead of a manual `sqrt(3.0)` formula in
`kemna_vorst`; a designated initializer for the `HiLo` struct in the Philox
implementation), plus one more suppression added to `.clang-tidy`
(`cppcoreguidelines-pro-bounds-constant-array-index`, for the same
deliberate, verified-safe hot-path array indexing rationale as Phase 0's
existing pointer-arithmetic suppression) for a pattern introduced by Phase
4's parallel accumulation engine.

### Thread-scaling and false-sharing

![Scaling chart](benchmarks/phase4-scaling.svg)

Measured (`bench/mc_bench.cpp`, `BM_MonteCarloEuropeanThreads`, median of 5
runs, European call, 2×10⁷ paths, Apple M3 Pro — 5 performance + 6 efficiency
cores, 11 physical/11 logical, no SMT — GCC 16, `-O3 -march=native`, Release):

| Threads | Speedup | Efficiency |
|---|---|---|
| 1 | 1.00× | 100% |
| 4 | 3.76× | 94.1% |
| 6 | 4.91× | 81.9% |
| 11 (physical core count) | **5.97×** | **54.2%** |

Amdahl's-law least-squares fit over N=1..11: serial fraction **f ≈ 0.088**.
Data beyond N=11 is not reported as scaling evidence: `logical_chunk_count()`
is fixed at `hardware_concurrency()=11` (per the determinism design above),
so thread counts past 11 have no additional chunks to claim — any apparent
movement there is measurement noise from this being a shared development
machine (Google Benchmark reported a load average of ~5 during the run, i.e.
real background contention for cores), not genuine additional parallel
throughput.

**Written analysis of the < 80% efficiency at physical core count**, per
CLAUDE.md's explicit fallback clause:

1. **Heterogeneous P+E cores with static equal-sized chunking is the primary
   suspect.** CLAUDE.md locks "static contiguous chunking" — every chunk gets
   an equal `path_count / 11` share regardless of which core executes it. The
   M3 Pro's 5 performance cores are meaningfully faster than its 6 efficiency
   cores for sustained arithmetic-heavy work (this pricer's hot loop:
   Philox + Acklam inverse-CDF + GBM step per path). Since `parallel_for`
   cannot return until *every* chunk completes, the round's wall-clock time
   is set by the slowest chunk — almost certainly one landing on an
   efficiency core — while performance cores that already finished their
   equal share sit idle. This is a direct, inherent consequence of pairing
   equal static chunking with heterogeneous cores, not a bug; a work-stealing
   or performance-weighted chunking scheme would very likely close much of
   this gap, but that's a different, dynamic work-division design than the
   one CLAUDE.md locks in for this phase.
2. **Shared, non-isolated benchmark machine.** This is a personal laptop
   being actively used during the session (load average ~5 reported
   alongside the measurements above), not a dedicated, isolated benchmark
   box — background processes compete for the same physical cores the
   benchmark is trying to use exclusively.
3. **Fixed per-round synchronization cost.** Every parallel round pays a
   constant tax (mutex acquisition, `notify_all`, latch countdown/wait)
   independent of path count; at a fixed total workload, spreading it across
   more workers means each one does proportionally less real work per unit
   of synchronization overhead paid.

None of these are addressed by writing a different pool implementation within
this phase's locked design (static chunking, hand-written pool); they are
honestly reported rather than hidden, per CLAUDE.md's explicit instruction
that a shortfall be accompanied by analysis, not silently smoothed over.

**False-sharing A/B**: `PaddedWelford` (padded to 128 bytes, one accumulator
per cache line) measured against a deliberately unpadded
`std::vector<WelfordAccumulator>` array, `hardware_concurrency()` threads each
repeatedly updating their own slot, median of 7 runs:

| Layout | Median time |
|---|---|
| Unpadded | 17.4 ms |
| Padded | 17.5 ms |

**No measurable difference on this machine and workload** — the two are
within each other's noise (stddev ~0.2-0.4ms on ~17.4ms). Reported as
measured, per CLAUDE.md's explicit instruction not to claim a benefit that
wasn't observed. `PaddedWelford` is still used throughout (it costs
essentially nothing and is the theoretically correct choice per the false
-sharing literature), but this specific measurement doesn't demonstrate a
benefit on Apple Silicon's cache-coherency implementation for this specific
access pattern — plausible contributing factors include M-series' unusually
large, sophisticated per-core caches and coherency protocol, or that this
workload's compute-per-write ratio (Welford's `add()` does real arithmetic
between memory accesses) isn't memory-bandwidth-bound enough for
cache-line contention to dominate. Not investigated further this session;
recorded as an honest null result rather than a claimed win.

## Phase 5 — Greeks and American options

**Householder QR least squares, tested standalone before LSM used it at all**
(`tests/linalg_test.cpp`): a known-answer overdetermined system
(3 points, hand-solved regression, β = [2/3, 1/2]) and an exactly-determined
3×3 system (hand-solved via elimination, β = [6, 15, -23]) both reproduced to
1e-9. Agreement with a normal-equations solver (formed explicitly,
test-only) on a well-conditioned Vandermonde system, and — the actual point
of choosing Householder over the normal equations per CLAUDE.md §5 —
**measured**, not asserted, superior accuracy on an ill-conditioned cubic
Vandermonde system built from four x-values clustered within 0.03% of each
other (recovering a known β = [2, -3, 5, -1] from noiseless data): Householder
QR's total coefficient error was smaller than the normal-equations solver's on
the same data (both computed from the identical `A`, `y`; the normal-equations
path explicitly forms AᵀA before solving, the QR path never does). All four
tests pass.

**Bump-size sweep — real data, not the textbook exponent assumed**
(`docs/benchmarks/phase5-bump-size-sweep.svg`; S=K=100, r=0.05, q=0, σ=0.20,
T=1, European call, N=200,000 paths, seed=777, h swept over
h/S ∈ [1e-6, 1] — seven orders of magnitude): delta's error stays flat around
6×10⁻⁴ across the entire measured range and only degrades once h/S exceeds
≈3×10⁻², so delta is not the binding constraint. Gamma's error is
catastrophic below h/S ≈ 1×10⁻⁴ (MC-noise regime — dividing a fixed-noise
difference by a shrinking h²), falls to a broad low-error plateau over
roughly h/S ∈ [3×10⁻³, 1×10⁻¹] (measured minimum 2.05×10⁻⁶ at
h/S = 3.16×10⁻³), then rises again past h/S ≈ 3×10⁻¹ (truncation regime,
error ~ h²) — exactly the two-regime shape with an interior optimum CLAUDE.md
§6 Phase 5 describes, confirmed by measurement rather than assumed. Default
`spot`/`vol`/`time` bump fraction set to 1% — inside the measured plateau, not
at the single-seed measured minimum (chosen deliberately: the minimum is
itself noisy, being measured off one fixed seed, and 1% is the standard,
seed-independent literature default). `rate`'s bump is a fixed 100bp absolute
(not independently swept — relative bumps are undefined at r near 0). Only
the spot/gamma trade-off was directly measured; vol/time reuse the same
relative-fraction reasoning since they share the same truncation-vs-noise
shape, per `docs/design/05-greeks-and-american.md` §2.3's stated scope.

**Common random numbers**: verified directly, not just inferred from smooth
Greeks — `CommonRandomNumbers.ZDrawIsIndependentOfScenarioParameters` asserts
bit-identical `standard_normal_variate` draws via `std::bit_cast<uint64_t>`.
This holds by construction (the RNG counter is keyed on `(seed, path_index,
draw_index)` only), but is asserted explicitly per CLAUDE.md §6 Phase 5's
requirement, not left implicit.

**FD Greeks vs. BSM analytic, four parameter combinations, all within 3 SE**
(`tests/greeks_test.cpp`, `ParameterMatrix/FiniteDifferenceGreeksVsAnalytic`,
N=300,000 paths): the analytic oracle is a tight (h/S ≈ 1e-5) deterministic
central difference on the already-validated (Phase 1) closed form, not a new
formula. Since `EuropeanGreeks` doesn't carry a per-Greek standard error, the
"3 SE" tolerance is a bound reconstructed in the test from the underlying
`McResult::standard_error` of each bumped call, propagated through the
central-difference formula **assuming independence between bumped scenarios**
— which CRN deliberately violates (the whole point of CRN is that bumped
scenarios are strongly *correlated*, hence lower true variance in the
difference). This makes the bound conservative (loose, not tight) in a known
direction, documented in `tests/greeks_test.cpp` rather than presented as an
exact confidence interval.

**A real bug found and fixed by CLAUDE.md's own required test**: `American
put >= immediate exercise value everywhere` (§6 Phase 5) initially failed for
a deep in-the-money put (S=60, K=100 — immediate exercise value 40) by far
more than 3 standard errors, and — the tell that this was a real bug and not
Monte Carlo noise — **the gap did not shrink as path count grew**: 0.51 at
N=50,000, 0.50 at N=200,000, 0.50 at N=800,000 (monitoring_points=10); it only
shrank as monitoring frequency increased (0.51 → 0.10 → 0.026 across
monitoring_points ∈ {10, 50, 200}), the signature of a discretization/
decision-timing bias, not sampling error. Root cause: the backward-induction
loop only ever compares exercise against continuation at monitoring dates
t = dt, 2dt, ..., T — it never considers exercising *at t=0 itself*, even
though that choice is always available and, for a deep ITM put, is close to
strictly better than waiting even one small dt. Unlike every other exercise
date, the t=0 decision is a single deterministic scalar comparison (spot at
t=0 is one number shared by every path, not a distribution), so it needed no
regression — just `price = max(intrinsic(spot), regression-based continuation
estimate)`, with `standard_error = 0` exactly when the intrinsic value wins,
since that decision carries no sampling noise. Fixed in both
`monte_carlo_lsm_american` and `reprice_against_frozen_policy` (the latter
replaying the *frozen* base-run decision rather than re-deciding, consistent
with every other frozen exercise date); `LsmPolicy` gained an
`exercise_at_inception` field to carry it. Re-verified: the deep-ITM gap is
now exactly 0.0 (not merely within tolerance) at every monitoring
frequency/path-count combination tested, since the corrected decision is
deterministic once it triggers.

**LSM vs. a fine (4,000-step) American binomial tree, six parameter
combinations spanning both option types, dividend and non-dividend cases,
within 3 SE** (`tests/lsm_test.cpp`, `ParameterMatrix/LsmVsBinomial`,
N=100,000 paths, 50 monitoring points, degree-3 Laguerre basis) — an
independently-computed reference (tree, not regression) per CLAUDE.md §2.5.
**American call equals European call on a non-dividend-paying underlying**
(`Lsm.AmericanCallEqualsEuropeanCallWithoutDividends`) — the sharpest
available correctness check, since early exercise being suboptimal is a
sharp theoretical fact, not a loose bound. **American put ≥ European put**
and **≥ immediate exercise value everywhere** (both, now exactly, per the bug
fix above) also verified. **Convergence in path count**
(`Lsm.ConvergesAsPathCountIncreases`): standard error and the deviation from
the binomial reference both shrink monotonically as N grows from 10,000 to
160,000 (SE: 0.093 → 0.046 → 0.023; |price − binomial|: 0.079 → 0.054 →
0.022).

**Simplification disclosed**: the Laguerre basis is implemented as a fixed,
non-templated degree-3 function in `src/pricers/lsm.cpp`, not exposed as a
pluggable template parameter on the public API as
`docs/design/05-greeks-and-american.md` §3.2 originally described. Convergence
in *basis degree* specifically (as opposed to path count) was not measured
this phase as a result — the internal implementation would need to become
template-parameterized first. Flagged here rather than silently narrowing
scope; degree-3 Laguerre is still the field-standard default from the
original Longstaff-Schwartz paper, so this does not affect correctness, only
the pluggability CLAUDE.md's design intent described.

**Frozen exercise boundary for American Greeks — quantified against naive
full-refit, per CLAUDE.md §6 Phase 5's "quantify the improvement"
requirement**: an American put delta (S=K=100, r=0.05, q=0, σ=0.25, T=1,
20 monitoring points) was estimated 40 independent times (independent seeds)
two ways — reusing a frozen base-run policy for the ± bumped repricing
(`reprice_against_frozen_policy`) versus refitting the LSM regression from
scratch at each bumped spot (`monte_carlo_lsm_american`) — and the standard
deviation of the resulting delta estimate across those 40 trials was compared,
swept over path count and bump size:

| path count | h    | frozen σ(delta) | naive-refit σ(delta) | naive/frozen |
|-----------:|-----:|-----------------:|----------------------:|--------------:|
| 2,000      | 0.05 | 0.1585            | 0.3222                 | **2.03×**      |
| 2,000      | 0.10 | 0.0985            | 0.1209                 | **1.23×**      |
| 5,000      | 0.05 | 0.0644            | 0.1639                 | **2.54×**      |
| 5,000      | 0.10 | 0.0583            | 0.0785                 | **1.35×**      |
| 5,000      | 0.25 | 0.0365            | 0.0427                 | 1.17×          |
| 5,000      | 0.50 | 0.0277            | 0.0287                 | 1.04×          |
| 20,000     | 0.05 | 0.0396            | 0.0487                 | 1.23×          |
| 20,000     | 0.10 | 0.0251            | 0.0384                 | 1.53×          |
| 20,000     | 0.25 | 0.0174            | 0.0173                 | 0.99×          |
| 20,000     | 0.50 | 0.0129            | 0.0117                 | 0.91×          |

The effect is a *relative* one, exactly as the underlying mechanism predicts:
refitting introduces an extra noise source (the fitted exercise boundary
moving with the bumped data) on top of ordinary Monte Carlo sampling noise,
so freezing helps most where that extra source is a large share of the total
— smaller path counts (noisier regression) and smaller bump sizes (which
divide by a smaller `2h`, amplifying whichever noise is present). At larger
path counts combined with larger bumps, the regression is well-conditioned
enough, and the bump large enough, that refitting barely perturbs the fitted
boundary, and the effect shrinks to the point of statistical noise (0.99×,
0.91× — not a real reversal, just no measurable difference at 40 trials).
Reported both directions rather than only the setting that favors the
mitigation. `tests/lsm_test.cpp`'s
`Lsm.FrozenBoundaryGreeksHaveLowerVarianceThanNaiveRefit` uses the
(N=5,000, h=0.10) row (naive/frozen = 1.35×) as a real, reproducible, and
non-cherry-picked-to-the-extreme regression test of the effect.

**Known, documented limitations** (per CLAUDE.md §6 Phase 5, stated plainly,
not hidden):
- LSM is a lower-bound estimator: the fitted continuation value is an
  approximation, and using it to make exercise decisions can only leave value
  on the table relative to the true optimal policy, never exceed it. Every
  LSM price in this project should be read as such, not as an exact value.
- Gamma for discontinuous payoffs (digitals, barriers near the boundary)
  remains this engine's weakest estimate under finite differences — the
  correct fix is a likelihood-ratio estimator, out of scope until the stretch
  goals (CLAUDE.md §7).

## Phase 6 — generated results

Regenerated live from the engine on every run of `python tools/generate_report.py`
(CLAUDE.md sec.6 Phase 6; see docs/design/06-cli-bindings-reporting.md sec.2.2/sec.5 for
why this is a delimited block rather than the whole document). Do not hand-edit the block
below — it is mechanically overwritten.

<!-- BEGIN GENERATED -->

_Live-regenerated by `python tools/generate_report.py` on 2026-08-17 16:20 UTC. This block is mechanically overwritten on every run -- do not hand-edit it; edit the narrative sections elsewhere in this document instead._

### CFA invariant results (live)

| Module | Invariant | Result | Measured deviation |
|---|---|---|---|
| LM4 | Cost of carry | PASS | 0.000e+00 |
| LM5 | Forward value at initiation is zero | PASS | 0.000e+00 |
| LM5 | Forward value during life | PASS | 0.000e+00 |
| LM9 | Put-call parity | PASS | 1.421e-14 |
| LM9 | Put-call forward parity | PASS | 0.000e+00 |
| LM10 | Binomial risk-neutrality | PASS | 0.000e+00 |
| LM10 | Binomial converges to BSM (monotonically, error<0.01 at n=5000) | PASS | 3.888e-04 |
| LM4/LM8 | No-arbitrage bounds | PASS | 0.000e+00 |
| LM8 | Monotonicity in spot | PASS | 0.000e+00 |

### Convergence: standard error vs. path count (live)

| Path count | Standard error |
|---:|---:|
| 1,000 | 0.469566 |
| 10,000 | 0.146957 |
| 100,000 | 0.046565 |
| 1,000,000 | 0.014701 |

Fitted log-log slope: **-0.5012** (theory: -0.5).

![convergence](benchmarks/generated-convergence.svg)

### Variance-reduction factors (live)

| Product | Technique | Variance-reduction factor |
|---|---|---:|
| European | antithetic | 3.98x |
| Arithmetic Asian | control variate (geometric Asian) | 590.19x |

### Bump-size sweep for finite-difference Greeks (live)

Measured gamma-error minimum this run: h/S = 3.162e-03, error = 2.054e-06.

![bump-size](benchmarks/generated-bump-size-sweep.svg)

### Thread scaling (live)

`hardware_concurrency()` on this machine: 11.

| Threads | Speedup vs. 1 thread |
|---:|---:|
| 1 | 1.00x |
| 2 | 1.81x |
| 5 | 3.67x |
| 11 | 6.22x |

Parallel efficiency at max thread count: **56.6%**.

![scaling](benchmarks/generated-scaling.svg)

<!-- END GENERATED -->

## Sections (populated as later phases land)

- CFA invariant results table
