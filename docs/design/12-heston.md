# Stretch Goal 5 — Heston stochastic volatility

Status: **implemented, Stretch Goal 5 gate passed** (wired through
mcd_cli/bindings/AWS demo despite the larger parameter surface)

## 1. Purpose

Per CLAUDE.md §7 item 5: "Heston stochastic volatility — QE scheme,
validated against the semi-analytic characteristic-function price." This
is the one stretch goal that genuinely extends the model boundary
CLAUDE.md §4 otherwise locks to constant-volatility GBM — but §7
pre-approves exactly this extension, so it isn't a relitigation of that
lock, just its one named exception.

**Scope, deliberately bounded**: European call/put only, priced two
ways — Andersen's (2008) Quadratic-Exponential (QE) Monte Carlo scheme,
and Heston's (1993) semi-analytic characteristic-function price (with the
Albrecher et al. "Little Trap" branch-cut fix). No exotics under Heston
in this pass — extending Asian/barrier/lookback/American to stochastic
volatility is a materially separate undertaking (each needs its own
discretization and its own oracle) and isn't what CLAUDE.md §7 asks for.

## 2. The model

$$dS_t = (r-q)S_t\,dt + \sqrt{v_t}\,S_t\,dW_t^S, \qquad
dv_t = \kappa(\theta - v_t)\,dt + \xi\sqrt{v_t}\,dW_t^v, \qquad
\text{corr}(dW^S, dW^v) = \rho\,dt$$

Five new parameters beyond GBM: `v0` (initial variance), `kappa`
(mean-reversion speed), `theta` (long-run variance), `xi` (vol-of-vol),
`rho` (spot/variance correlation, typically negative — the leverage
effect). The Feller condition `2*kappa*theta > xi^2` keeps the CIR
variance process away from zero analytically; QE's entire purpose is
staying correct and non-negative **even when Feller fails**, which real
calibrated parameter sets routinely do — so the test plan (sec.6)
deliberately exercises the sub-Feller regime, not just the well-behaved
one.

```cpp
// include/mcd/models/heston.hpp
namespace mcd::models {
struct HestonParams {
    double spot;
    double rate;
    double carry_yield;
    double v0;
    double kappa;
    double theta;
    double xi;
    double rho;
    double time;
};
} // namespace mcd::models
```

## 3. QE Monte Carlo (Andersen 2008)

Unlike GBM (Phase 2's one-shot exact log-Euler solution), Heston has no
closed-form terminal distribution — the variance process must be
discretized across `monitoring_points` steps even for a terminal-only
European payoff, since `v_t` is itself path-dependent. Per step `dt`:

**Variance update** — moment-matched to the true (non-central
chi-squared) transition distribution, switching representation based on
`psi = s²/m²` (a coefficient-of-variation proxy) against a threshold
`psi_c` (1.5, per the paper):

- `psi <= psi_c` (low noise): `v_{t+dt} = a(b + Z_v)²`, a squared,
  shifted normal — matches the true distribution's first two moments
  exactly, always non-negative by construction.
- `psi > psi_c` (high noise, where a normal-based moment match would go
  negative): `v_{t+dt}` drawn from a mixture with a point mass at zero
  and an exponential tail, inverted via a uniform `U_v` — this is the
  branch that makes QE robust exactly where the naive schemes
  (absorption, reflection, full truncation) are known to be biased.

**Log-spot update** — the martingale-corrected (`K0`–`K4`) discretization
from the same paper, using the central (`gamma1 = gamma2 = 0.5`) weighting:

```
ln S_{t+dt} = ln S_t + K0 + K1*v_t + K2*v_{t+dt} + sqrt(K3*v_t + K4*v_{t+dt}) * Z_S
```

where `K0..K4` are closed-form functions of `kappa, theta, xi, rho, dt`
that fold the `rho` correlation into the drift/diffusion coefficients
analytically, leaving `Z_S` an *independent* standard normal (the
correlation is already accounted for through `K1, K2`, not through
correlating `Z_S` with `Z_v` directly).

**RNG draws per step**: reuses the existing stream convention
(`docs/design/03-exotics-variance-reduction.md` sec.7) — stream 0 gives
`Z_S`; stream 1's raw uniform is reused two ways, either transformed
through the existing `inverse_standard_normal_cdf` to get `Z_v` (low-noise
branch) or used directly as `U_v` (high-noise branch) — exactly one
Philox call per `(path, step)` for the variance side, no wasted draws,
same determinism guarantee as every other pricer (path *i* draws the
same numbers regardless of thread count).

## 4. Semi-analytic characteristic-function price

Heston (1993)'s own P1/P2 decomposition, with the branch-cut-safe form
from Albrecher, Mayer, Schoutens & Tistaert (2007) — the well-known fix
for the original formula's numerical instability at long maturities/high
vol-of-vol, where the naive principal-branch complex logarithm jumps
discontinuously as the integration variable increases. Requires
`std::complex<double>` (standard library, no new dependency) and
numerical integration of an oscillatory semi-infinite integral.

**Quadrature, self-verified, not a memorized table** — same discipline
Stretch Goal 3 applied to Sobol's direction numbers (CLAUDE.md §2.5):
Gauss-Legendre nodes and weights are computed from scratch via
Newton-Raphson root-finding on Legendre polynomials (a standard,
derivable numerical method), not transcribed from an external table. The
correctness check is direct and external to the Heston pricer entirely:
the quadrature must exactly reproduce `∫x^k dx` over `[-1,1]` for every
`k` up to its exactness degree, and over the truncated integration domain
this pricer actually uses. The truncation bound is chosen empirically —
increase it until the price stops moving beyond the 3-SE tolerance the
QE comparison uses, and that bound is recorded, not assumed a priori.

## 5. Independent verification (CLAUDE.md §2.5 — every method needs an oracle)

Three checks, in increasing order of how much they lean on outside data:

1. **Limiting case, fully self-contained**: as `xi -> 0` with `v0 = theta`,
   variance stops moving and Heston must reduce to Black-Scholes-Merton
   with `sigma = sqrt(theta)` — an exact closed-form check against
   Phase 1's own `black_scholes_merton`, requiring nothing external.
2. **Put-call parity on the semi-analytic price**:
   `C - P = S0*e^(-qT) - K*e^(-rT)` — the same identity Phase 1 already
   proves for every other pricer in this engine, still an exact identity
   under Heston since it only relies on no-arbitrage, not on the specific
   volatility dynamics.
3. **A published reference price**, fetched (not recalled from memory,
   per CLAUDE.md §2.5's whole rationale) from a citable external source —
   the same practice Phase 2 used for the Philox test vectors and
   Acklam's coefficients (`docs/validation-report.md` Phase 2: "fetched
   and cross-verified... not recalled"). I'll fetch a well-known,
   widely-cited Heston parameter set with a published price at
   implementation time and record the source URL in the test file
   itself, rather than propose one from memory here.

## 6. Test plan

- **Gauss-Legendre self-test**: exact polynomial integration, as sec.4
  describes — independent of the Heston pricer.
- **QE MC within 3 SE of the semi-analytic price**, across a parameter
  matrix that explicitly includes both Feller-satisfied and
  Feller-violated cases (the latter forcing real coverage of the
  `psi > psi_c` branch — logged, not just assumed exercised).
- **Limiting-case and put-call-parity checks** from sec.5, items 1-2.
- **Reference-price check** from sec.5 item 3, within a stated tolerance
  of the fetched published value.
- **No NaN/negative variance ever**, across 10⁶ paths at a deliberately
  extreme sub-Feller parameter set (`xi` large relative to
  `kappa*theta`) — a regression test for the exact failure mode QE exists
  to fix.
- **Determinism**: identical seed -> bitwise-identical QE price, same
  discipline as every other Monte Carlo pricer in this project.
- **Convergence in step count**: QE bias should shrink as
  `monitoring_points` increases, reported over a small grid (this is a
  genuinely biased discretization, unlike GBM's exact simulation — the
  bias should be measured and disclosed, not hidden).

## 7. What stays out of scope

- No Asian/barrier/lookback/American under Heston.
- No calibration (fitting `v0, kappa, theta, xi, rho` to market prices) —
  this pricer takes model parameters as given, same as every other
  pricer in this engine takes `sigma` as given.
- No correlation between the Heston variance process and interest
  rates/dividends — `r` and `q` stay deterministic constants, matching
  every other product in this engine.

## 8. Open questions for you

1. **Exposure**: Heston adds five new required parameters
   (`v0, kappa, theta, xi, rho`) beyond every existing product's schema —
   a materially larger `mcd_cli`/bindings/AWS surface than Stretch Goals
   1-3 added. Given that, I'd propose **engine + test only** for this
   pass (no CLI/bindings/AWS wiring) unless you want it exposed anyway;
   your call on the last three stretch goals has consistently been "wire
   it through everywhere," so say so explicitly if that's still the
   preference here.
2. **Reference price source**: confirm I should fetch a published Heston
   benchmark via WebFetch/WebSearch at implementation time (sec.5 item
   3) rather than you supplying one, and that citing the source URL in
   the test file (not reproducing any copyrighted exhibit text, just the
   numbers) is the right way to record it.
3. **Step count default**: propose `monitoring_points` defaults matching
   the existing path-dependent products' convention (caller-specified,
   no engine-side default) — confirm, or say if you want a documented
   minimum given QE's discretization bias.
