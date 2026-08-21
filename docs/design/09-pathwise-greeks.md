# Stretch Goal 2 — Pathwise-Derivative Greeks and a Written Comparison

Status: **implemented, Stretch Goal 2 gate passed**

## 1. Purpose

Per CLAUDE.md §7 item 2: pathwise-derivative Greeks, plus a written
comparison of FD vs. pathwise vs. LR (Stretch Goal 1) — variance, bias,
cost, and **where each breaks**. The "where each breaks" clause is doing a
lot of work here and is the actual point of implementing a third method
rather than stopping at LR: pathwise is the textbook complement to LR
precisely because their failure modes are opposite.

## 2. The method, derived here

Pathwise (a.k.a. infinitesimal perturbation analysis) exchanges
differentiation and expectation the *other* way from LR — instead of
differentiating the density, it differentiates the **payoff along the
simulated path**, holding the random draws fixed (again, the same
"what's held fixed" CRN convention used throughout this project):

```
∂/∂θ E[h(S_T(θ))] = E[∂h/∂S_T · ∂S_T/∂θ]
```

For GBM, `S_T = S_0 exp((r-q-σ²/2)T + σ√T·Z)`, so the pathwise sensitivities
of the terminal spot itself are simple closed forms:

```
∂S_T/∂S_0 = S_T / S_0
∂S_T/∂σ   = S_T · (√T·Z − σT)
∂S_T/∂r   = S_T · T
```

For a European call, `∂h/∂S_T = 1{S_T > K}` (the payoff's slope where it's
differentiable — a.e. everywhere except the strike itself, a measure-zero
event that's never exactly sampled). Multiplying gives pathwise delta/vega/
rho; rho needs the same discount-factor product-rule term LR's rho needed
(sec.3 of `docs/design/08-likelihood-ratio-greeks.md`), for the same
reason.

## 3. Where pathwise breaks — the actual content of this stretch goal

**Gamma is structurally undefined for pathwise, for every product, not a
gap I'm choosing to skip.** Gamma would require `∂²h/∂S_T²`, the derivative
of an indicator function — a Dirac delta, not a number. This isn't a
missing implementation; it's why the method doesn't exist for second-order
Greeks in the literature at all. Documented, not silently omitted.

**Delta is not just noisy but *systematically, silently wrong* for
discontinuous payoffs.** For a digital, `h` is a step function everywhere;
`∂h/∂S_T = 0` at every point except exactly the strike, so the naive
pathwise delta estimator is **zero with probability 1** — not imprecise,
*wrong*, and deceptively confident about it (its own reported standard
error is also ~0, since every path agrees on the same wrong answer). This
is the sharpest, most concrete version of "where it breaks" available, and
it's directly measurable: run it, show it converges to 0, show the true
digital delta isn't 0 (already have this from the LR/analytic
cross-check in Phase 5/Stretch Goal 1), and put the number in the
validation report rather than only asserting the failure mode in prose.

**Where it works well**: European (delta/vega/rho) and Asian
(arithmetic/geometric, both strike styles — the payoff is still
piecewise-linear in the path average, so the same pathwise machinery
applies with the path average's own pathwise sensitivity summed across
steps) are smooth-enough payoffs for pathwise to be valid, and pathwise is
*cheap* relative to LR and FD: one extra multiply per path, no bumped
re-pricing, no score-function overhead, and — unlike LR — it doesn't
amplify variance on paths where the payoff itself carries no information
(LR's score term adds noise from every path regardless of whether that
path's payoff is informative; pathwise's derivative is exactly zero on
paths where the option is out of the money, contributing no noise there
at all).

## 4. Interface

```cpp
// include/mcd/greeks/pathwise.hpp
namespace mcd::greeks {

struct PathwiseGreeksResult {
    double value = 0.0;
    double standard_error = 0.0;
};

// No gamma field at all -- not optional-and-empty like LR's barrier theta,
// genuinely absent from the type, since it isn't just "not computed for this
// product" but structurally undefined for every product this method covers.
struct PathwiseGreeks {
    PathwiseGreeksResult delta, vega, rho;
};

[[nodiscard]] PathwiseGreeks pathwise_european(
    double spot, double strike, double rate, double carry_yield, double vol, double time,
    OptionType type, std::uint64_t path_count, std::uint64_t seed) noexcept;

[[nodiscard]] PathwiseGreeks pathwise_asian(
    double spot, double strike, double rate, double carry_yield, double vol, double time,
    OptionType type, StrikeStyle strike_style, AverageStyle average_style,
    int monitoring_points, std::uint64_t path_count, std::uint64_t seed) noexcept;

// Deliberately demonstrates the failure mode from sec.3 rather than refusing to compile
// a digital pathwise delta -- the point is to *measure* the bias, not just assert it.
// Documented prominently as broken; not exposed through mcd_cli/bindings/the AWS demo
// (sec.6 open question 2).
[[nodiscard]] PathwiseGreeksResult pathwise_digital_delta_naive_and_broken(
    double spot, double strike, double rate, double carry_yield, double vol, double time,
    OptionType type, DigitalStyle style, double cash_amount, std::uint64_t path_count,
    std::uint64_t seed) noexcept;

} // namespace mcd::greeks
```

## 5. The written comparison (CLAUDE.md's explicit deliverable)

A table in `docs/validation-report.md`, populated from real measurements
across the products already validated for both methods (European,
digital), covering:

- **Variance**: standard error at matched `(path_count, seed)` for
  delta/vega/rho where both FD and pathwise are valid.
- **Bias**: FD has real, measurable truncation bias (Phase 5's bump-size
  study); pathwise and LR are unbiased *where defined*. The digital delta
  failure (sec.3) is the sharpest bias example in this project.
- **Cost**: pathwise needs 1 path evaluation per Greek (or fewer, since
  several Greeks share the same path); FD needs 2–3 bumped re-pricings per
  Greek; LR needs 1 path evaluation but extra per-path arithmetic for the
  score. Measured wall-clock, not just asymptotic argument.
- **Where each breaks**: FD — discontinuous payoffs, especially gamma
  (Phase 5 finding, Stretch Goal 1's whole motivation). Pathwise —
  discontinuous payoffs entirely (delta wrong, not just noisy), gamma for
  every product. LR — nothing breaks *outright*, but variance is
  comparatively higher for smooth payoffs where pathwise is cheap and
  precise (measured, not assumed).

## 6. Open questions for you

1. **Scope**: European + Asian for the working case, digital for the
   documented-failure demonstration (matching Stretch Goal 1's own
   terminal + one path-dependent product scope). Confirm, or say if you
   want barrier's pathwise delta/vega too (barrier's payoff is also
   piecewise-linear conditional on not having breached, so pathwise
   applies the same way LR's path-dependent generalization did — feasible,
   just additional scope).
2. **Exposure**: the design doc's interface above proposes wiring the two
   *working* estimators (`pathwise_european`, `pathwise_asian`) through
   `mcd_cli`/bindings/the AWS demo, same as Stretch Goal 1's "everywhere"
   choice — but proposing the *broken* digital estimator stays
   engine/test-only (a documented failure mode, not a product feature to
   expose to demo users). Confirm.
