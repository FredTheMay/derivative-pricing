# Stretch Goal 1 — Likelihood-Ratio Greeks

Status: **implemented, Stretch Goal 1 gate passed**

## 1. Purpose

Per CLAUDE.md §7 item 1 ("highest value of anything here"): a likelihood-ratio
(LR, a.k.a. score-function) Greeks estimator that fixes the documented weak
point of finite-difference Greeks — gamma for discontinuous payoffs (digitals,
barriers near the boundary), called out explicitly in Phase 5's validation
report as "this engine's weakest estimate."

## 2. Why LR fixes what FD can't

A cash-or-nothing digital's payoff is a step function in `S_T`. Its true delta
is a Dirac-delta-weighted density (infinite at the strike in the limiting
case); its gamma is worse. Finite differences bump `S_T` and re-evaluate a
discontinuous function — near the discontinuity, the central-difference
quotient is dominated by whether the bump happened to cross the jump, not by
the option's actual local sensitivity, and gamma (dividing by `h²`) amplifies
this into severe noise no matter how the bump size is tuned (Phase 5's
bump-size sweep already showed this trade-off has no good resolution for
smooth payoffs, let alone discontinuous ones).

The likelihood-ratio method never differentiates the payoff at all — it
differentiates the *path's probability density* with respect to the
parameter, moving the derivative off the discontinuous function entirely:

```
∂/∂θ E[h(S_T)] = ∂/∂θ ∫ h(s) f(s|θ) ds = ∫ h(s) (∂f/∂θ) ds
                = E[h(S_T) · (∂f/∂θ)/f] = E[h(S_T) · ∂ln f/∂θ]
```

`h` (the payoff) never needs to be smooth or even continuous for this to
work — only the density `f` does, and GBM's lognormal transition density is
infinitely smooth. This is exactly why LR is the standard fix for
discontinuous-payoff Greeks in the literature (Broadie & Glasserman 1996,
*Estimating Security Price Derivatives Using Simulation*) and why CLAUDE.md
names it specifically for digitals and barriers.

## 3. Score functions — derived here, not copied from any source

GBM's exact solution: `ln(S_T) = ln(S_0) + (r-q-σ²/2)T + σ√T·Z`, `Z ~ N(0,1)`.
Differentiating the lognormal transition density's log with respect to each
parameter (full derivation in code comments at the point of use — this is
public-domain applied probability, not curriculum material) gives, with
`μ = r - q - σ²/2`:

```
∂ln f/∂S_0 = Z / (S_0 σ √T)                                    (delta score)
∂²ln f/∂S_0² + (∂ln f/∂S_0)² = (Z² − σ√T·Z − 1) / (S_0² σ² T)   (gamma score)
∂ln f/∂σ   = (Z² − 1)/σ − √T·Z                                  (vega score)
∂ln f/∂r   = √T·Z/σ                                              (rho score, before
                                                                   the extra −T·E[h]
                                                                   term from
                                                                   differentiating
                                                                   the discount factor)
∂ln f/∂T   = (Z² − 1)/(2T) + μZ/(σ√T)                            (theta score, before
                                                                   the extra −r·E[h]
                                                                   term and the sign
                                                                   flip to match this
                                                                   project's theta
                                                                   convention)
```

I re-derived all five independently (score-function method, holding the
*other* three market/model parameters and the standard normal draw `Z`
fixed — the same "what's held fixed" convention CRN already relies on
throughout this project) rather than trusting memory of the literature
formulas; delta/gamma/vega match the published Broadie-Glasserman results
exactly, which is the actual check that matters. Every score is a testable,
closed-form expression — precisely the kind of thing CLAUDE.md §2.5 requires
an independent reference for, and here the reference is *analytic*: the
score-based estimator must converge to the same value FD/BSM analytic Greeks
already give for smooth payoffs (European), which becomes the test oracle for
correctness before trusting it on payoffs where no other oracle exists.

## 4. Scope: European, digital, and single barrier — not every product

CLAUDE.md's own motivating text names "digitals and barriers" specifically.
Extending LR to path-dependent products requires summing the score across
every step's transition density (the path's joint density factors as a
product of per-step transition densities, so its log is a sum, and so is the
score) — worked out here for barrier and generalizable the same way to Asian/
lookback, but not implemented for those in this pass, since CLAUDE.md's
stated highest-value target is specifically the discontinuous-payoff case.
For barrier specifically (discrete monitoring, `n` steps of size `Δt = T/n`):

- **Delta and gamma**: only the *first* step's transition density depends on
  `S_0` (later steps condition on `S_1, S_2, ...`, not on `S_0` directly), so
  the barrier's delta/gamma scores use exactly the terminal-case formulas
  above with `T → Δt` and `Z → Z_1` (the first step's draw) — a known result
  of the path-dependent LR generalization, not a simplification I'm
  inventing.
- **Vega**: `σ` appears in *every* step's density, so the vega score is the
  sum over all `n` steps of the terminal-case vega score evaluated at each
  step's own `(Δt, Z_i)`.

## 5. Interface

```cpp
// include/mcd/greeks/likelihood_ratio.hpp
namespace mcd::greeks {

struct LrGreeksResult {
    double value;           // the Greek's point estimate
    double standard_error;  // LR estimators are themselves Monte Carlo
                             // averages -- they get a real standard error,
                             // not just a point number, same statistical
                             // treatment as every other MC result in this
                             // project.
};

struct LrGreeks {
    LrGreeksResult delta, gamma, vega, theta, rho;
};

// European and digital: dispatches on a payoff functor, terminal-spot only.
[[nodiscard]] LrGreeks likelihood_ratio_european(
    double spot, double strike, double rate, double carry_yield, double vol, double time,
    OptionType type, std::uint64_t path_count, std::uint64_t seed) noexcept;

[[nodiscard]] LrGreeks likelihood_ratio_digital(
    double spot, double strike, double rate, double carry_yield, double vol, double time,
    OptionType type, DigitalStyle style, double cash_amount, std::uint64_t path_count,
    std::uint64_t seed) noexcept;

// Barrier: path-dependent scores as derived in sec.4.
[[nodiscard]] LrGreeks likelihood_ratio_barrier(
    double spot, double strike, double barrier, double rate, double carry_yield, double vol,
    double time, OptionType type, BarrierDirection direction, BarrierKnock knock,
    double rebate, int monitoring_points, std::uint64_t path_count,
    std::uint64_t seed) noexcept;

} // namespace mcd::greeks
```

Reuses the existing RNG counter scheme (`standard_normal_variate(seed,
path_index, draw_index)`) unchanged — same streaming, zero-heap-allocation
architecture as every other pricer, since each path only needs `h(S_T)` (or
`h(path)`) and the handful of `Z` draws it already generated, accumulated via
`WelfordAccumulator` exactly like every other MC estimator in this project
(so LR Greeks get bitwise determinism across thread counts for free, from the
same architecture, not a separate guarantee to prove).

## 6. Test plan

- **Delta/gamma/vega vs. BSM analytic Greeks for European** (within 3 SE) —
  the sanity check that the re-derived score functions are actually correct,
  using the one product where an independent analytic oracle already exists.
- **LR vs. FD, quantified, on digitals** — the actual point of this stretch
  goal. Measure both estimators' gamma standard error (and, since the true
  digital gamma is a Dirac-delta-weighted density, not compare against a
  smooth-payoff "3 SE of a scalar" oracle but instead) measure how much each
  estimator's own reported standard error blows up as spot approaches the
  strike — LR's should stay bounded; FD's should not, on the existing Phase 5
  bump-size infrastructure. Report the ratio, not just assert LR is "better."
- **LR vs. FD on a barrier near its barrier level** — same comparison, for
  the path-dependent case.
- **Determinism**: bitwise-identical LR Greeks across thread counts is not
  applicable here (LR Greeks are always single-threaded per the existing FD
  Greeks precedent, sec.7 open question below) — instead, identical-seed
  reproducibility (two runs, same seed, bit-identical result) is the
  determinism test that applies.

## 7. Open questions for you

1. **Threaded or single-threaded?** Phase 5's FD Greeks are single-threaded
   (matching how Phase 2 preceded Phase 4). Proposing the same here for LR
   Greeks — consistent precedent, and LR Greeks are cheap enough (no bumped
   re-pricing, just one pass accumulating several running sums per path)
   that single-threaded throughput isn't a real bottleneck. Confirm, or say
   if you want it parallelized now.
2. **Expose LR Greeks in `mcd_cli`/the Python bindings/the AWS demo now, or
   as a follow-up?** Proposing: implement and validate the engine-level
   primitive in this pass (matching CLAUDE.md §7's actual ask), and treat
   CLI/bindings/demo exposure as a fast, mechanical follow-up once the
   estimator itself is proven correct — not bundled into this same design
   doc's gate, so the numerical work isn't blocked on plumbing.
