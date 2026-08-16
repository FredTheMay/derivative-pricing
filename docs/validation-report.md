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
- Phase 5 (Greeks and American options): not started.
- Phase 6 (CLI, bindings, reporting): not started.
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

## Sections (populated as later phases land)

- CFA invariant results table
- Bump-size sensitivity study (finite-difference Greeks)
- LSM American option validation against binomial reference
- Known limitations (documented, not hidden): LSM low-bias estimator; gamma for
  discontinuous payoffs under finite differences
