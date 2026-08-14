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
- Phase 3 (exotics and variance reduction): not started.
- Phase 4 (parallelism): not started.
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

## Sections (populated as later phases land)

- CFA invariant results table
- Convergence plots (log-log RMSE vs. paths, slope fit)
- Variance-reduction factor table (antithetic, control variate, Brownian bridge)
- Thread-scaling curve and Amdahl serial-fraction fit
- False-sharing A/B benchmark
- Bump-size sensitivity study (finite-difference Greeks)
- LSM American option validation against binomial reference
- Known limitations (documented, not hidden): LSM low-bias estimator; gamma for
  discontinuous payoffs under finite differences
