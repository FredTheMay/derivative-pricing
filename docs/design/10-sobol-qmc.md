# Stretch Goal 3 — Sobol QMC with Brownian-Bridge Construction

Status: **implemented, Stretch Goal 3 gate passed** (7-dimension cap
accepted; wired through mcd_cli/bindings/AWS demo with the cap enforced
as validation)

## 1. Purpose

Per CLAUDE.md §7 item 3: Sobol quasi-Monte Carlo (QMC) with Brownian-bridge
path construction — "should beat the −0.5 convergence slope; prove it on
the same log–log plot" as Phase 3's plain-MC convergence test
(`tests/convergence_test.cpp`).

## 2. The Sobol construction — derived and independently verified, not
copied from a reference table

A Sobol sequence in dimension `j` needs a primitive polynomial over GF(2)
of some degree `s_j` and a set of "direction numbers" derived from it. The
standard published tables (Joe & Kuo and others) exist because *choosing*
direction numbers well (beyond the minimum required for correctness)
improves the sequence's discrepancy further — but the *correctness*
requirement itself only needs (a) a genuinely primitive polynomial and (b)
any odd initial direction-number seed. I don't have reliable access to
transcribe an external table without risking a silent, hard-to-detect
error (the exact failure mode CLAUDE.md §2.5 exists to prevent — a subtly
wrong direction-number table can still *look* like a working low-discrepancy
sequence in casual testing), so this implementation uses only what can be
verified from first principles in this session:

- **Primitive polynomials, degrees 1–6, confirmed by direct LFSR
  simulation** (not memory): `x+1`, `x²+x+1`, `x³+x+1`, `x³+x²+1`,
  `x⁴+x+1`, `x⁵+x²+1`, `x⁶+x+1` — each checked by simulating its
  Fibonacci LFSR from a nonzero seed and confirming the period is exactly
  `2^degree − 1` (maximal length, the defining property of a primitive
  polynomial), a self-contained, from-scratch check with no external
  dependency. Gives **7 usable dimensions** (dimension 1 is the trivial
  degree-0 van der Corput sequence, needing no polynomial at all).
- **Initial direction numbers set to the simplest valid choice, `m_i = 1`
  for every `i`** — always a legal choice (the only formal requirement is
  `m_i` odd and `< 2^i`), trading discrepancy-optimality for a
  from-scratch-verifiable implementation. Documented as a deliberate,
  disclosed simplification, not presented as the industrial-strength
  Joe-Kuo construction.
- **Correctness check beyond "it runs"**: the defining property of a
  low-discrepancy sequence — 1-D stratification (among the first `2^k`
  points of any dimension, every one of the `2^k` equal sub-intervals
  contains exactly one point) — is directly testable without any external
  reference data, and is the actual independent check this implementation
  is validated against per CLAUDE.md §2.5, not just "the price looks
  right."

## 3. Brownian-bridge path construction

Separate from Phase 3's Brownian-bridge *continuity correction* for
discretely-monitored barriers (`docs/design/03-exotics-variance-
reduction.md`) — this is Brownian-bridge *path construction*
(Caflisch-Morokoff-Owen 1997), which reorders which Sobol dimension drives
which time step so that the *low* dimensions (where Sobol's discrepancy is
best) carry the *most important* part of the path: dimension 1 drives the
terminal point, dimension 2 the midpoint, dimension 3 the quarter-points,
and so on by recursive bisection — concentrating QMC's advantage where it
matters most, which is the entire point of pairing Sobol with a bridge
rather than sequential (first-to-last) step ordering.

## 4. Scope

Given the 7-dimension budget from sec.2, this pass covers:

- **European** (1 dimension — trivial bridge, since there's only one
  point to construct).
- **Arithmetic Asian, fixed strike**, up to **7 monitoring points** — the
  Brownian-bridge path construction actually matters here. The 7-point cap
  is a direct, disclosed consequence of sec.2's from-scratch-verified
  dimension budget, not an arbitrary product limitation.

## 5. Interface

```cpp
// include/mcd/core/sobol.hpp
namespace mcd {

// Up to 7 dimensions (sec.2). Deterministic, stateless per (dimension, index) --
// no counter/seed needed, unlike Philox: Sobol sequences are not randomized.
[[nodiscard]] double sobol_point(unsigned dimension, std::uint64_t index) noexcept;

} // namespace mcd

// include/mcd/models/brownian_bridge_path.hpp
namespace mcd::models {

// Maps `n` Sobol-QMC uniforms (already inverse-CDF-transformed to standard normals) to
// a GBM path's per-step terminal spots, in Brownian-bridge order (sec.3). n <= 7.
[[nodiscard]] std::vector<double> brownian_bridge_gbm_path(
    const GbmParams& params, int monitoring_points, std::span<const double> normals) noexcept;

} // namespace mcd::models

// include/mcd/pricers/qmc.hpp
namespace mcd::pricers {

struct QmcResult {
    double price = 0.0;
    // No standard_error: Sobol is deterministic, not a random variable across paths in
    // the same sense plain MC is. See sec.6 for how this project measures QMC accuracy
    // instead (absolute error vs. the known analytic price, not a statistical SE).
};

[[nodiscard]] QmcResult qmc_sobol_european(double spot, double strike, double rate,
                                            double carry_yield, double vol, double time,
                                            OptionType type, std::uint64_t path_count) noexcept;

[[nodiscard]] QmcResult qmc_sobol_asian(double spot, double strike, double rate,
                                         double carry_yield, double vol, double time,
                                         OptionType type, StrikeStyle strike_style,
                                         int monitoring_points, std::uint64_t path_count) noexcept;

} // namespace mcd::pricers
```

No `seed` parameter — Sobol sequences are deterministic given `(dimension,
index)`, matching the RNG's own "deterministic given inputs" spirit but
without a seed to key on, since there's nothing to randomize.

## 6. Proving "beats the −0.5 slope" — the actual deliverable

Phase 3's `Convergence.LogLogSlopeIsMinusOneHalf` test uses plain MC's own
reported standard error as a stand-in for RMSE (valid for an unbiased
estimator, per that test's own comment). Sobol is deterministic, so it has
no standard error in that sense — the fitting comparison is each method's
**absolute error against the known analytic BSM price** at matched path
counts, which is the metric QMC's advantage is actually about (a smaller
true approximation error, not a smaller variance of a randomized
estimator). `tests/qmc_test.cpp` fits both log-log slopes (plain MC's
absolute-error-vs-BSM and Sobol's absolute-error-vs-BSM) over the same
path-count grid `docs/benchmarks/phase3-convergence.svg` used, and asserts
Sobol's fitted slope is **steeper than −0.5** (i.e., more negative,
converging faster) — real measured data, reported whichever way it comes
out, not assumed in advance.

## 7. Test plan

- Primitive-polynomial LFSR maximal-period check (sec.2), for all 7
  polynomials.
- 1-D equidistribution/stratification check (sec.2) for at least 2
  dimensions.
- `qmc_sobol_european` price within a tight, deterministic tolerance of
  BSM analytic (not "3 SE" — there's no SE; a fixed, generously-loose
  numerical tolerance justified by the measured convergence rate at the
  tested path count).
- The log-log slope comparison from sec.6, with real fitted numbers
  printed and asserted steeper than −0.5.
- `qmc_sobol_asian` cross-checked against Phase 3's plain-MC arithmetic
  Asian pricer at a shared, generous tolerance (no independent closed form
  exists for arithmetic Asian, same situation Phase 3's own MC pricer was
  in).

## 8. Open questions for you

1. **7-dimension cap, honestly disclosed rather than papered over with an
   unverified external table** — confirm this trade (correctness-verified
   but capped at 7 monitoring points for the path-dependent product) is
   the right call, versus you'd rather I look for another way to extend
   dimension count safely.
2. **Exposure**: propose engine + test only for this stretch goal (not
   wired through `mcd_cli`/bindings/the AWS demo) — QMC's dimension cap
   makes it a poor fit for arbitrary user-specified `monitoring_points`
   through a public API, unlike Stretch Goals 1–2's estimators, which have
   no such structural limitation. Confirm, or say if you want it exposed
   anyway with the cap enforced as a request-validation rule.
