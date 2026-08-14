# Phase 3 — Exotics and Variance Reduction

Status: **implemented, Phase 3 gate passed** (see `docs/validation-report.md`
for results, including a real Phase 1 lookback formula bug this phase's
independent Monte Carlo oracle found and fixed)

## 1. Purpose

Implement Monte Carlo pricing for path-dependent products (Asian, barrier,
lookback) and digitals, then three variance-reduction techniques: antithetic
variates, the geometric-Asian control variate for arithmetic Asian, and a
Brownian-bridge continuity correction for discretely-monitored barriers. Per
CLAUDE.md §6 Phase 3.

## 2. The architectural question — please review before I implement

Phase 2's `Payoff` concept (`operator()(double terminal_spot) -> double`) is
correct for European and digital options: their payoff depends only on the
terminal price. Asian, barrier, and lookback payoffs depend on the **whole
path** — a running average, a running min/max, or whether a level was ever
crossed. That needs a different shape of payoff object, one that accumulates
state as the path unfolds rather than seeing only the final price.

**Proposed new concept**, alongside (not replacing) Phase 2's `Payoff`:

```cpp
template <typename P>
concept PathPayoff = requires(P& p, double price) {
    { p.observe(price) } -> std::same_as<void>;
    { p.result() } -> std::convertible_to<double>;
};
```

Each product's payoff type (`AsianPayoff`, `BarrierPayoff`,
`LookbackFixedPayoff`, `LookbackFloatingPayoff`) is a small stack-allocated
struct holding only running scalars — a sum, a count, a min, a max, a
breached flag. Never the path itself. This is actually a *tighter* memory
bound than "streaming, fixed-size stack buffer" (CLAUDE.md §4) — it's O(1)
regardless of step count, no buffer at all, because none of these payoffs
need anything from the path except a running aggregate.

The multi-step pricer drives this generically:

```cpp
template <PathPayoff P, typename... Args>
McResult monte_carlo_path_dependent(double spot, double rate, double carry_yield,
                                     double vol, double time, int monitoring_points,
                                     std::uint64_t path_count, std::uint64_t seed,
                                     bool antithetic, Args&&... payoff_args);
```

Per path: construct a fresh `P` on the stack from `payoff_args...` (e.g. an
`AsianPayoff` from `{strike, type, style}`), step the GBM forward
`monitoring_points` times using draw index = step number (this is exactly
what Phase 2's counter scheme was built to support — see
`docs/design/02-monte-carlo-core.md` §4), call `observe()` after each step,
call `result()` once at the end, discount, accumulate. `monitoring_points`
**is** the barrier's `monitoring_frequency` parameter from CLAUDE.md's
product list — the number of discrete observation dates across `[0,T]`.

**Convention**: `observe()` is called only at the `monitoring_points` future
dates (after each step), not at `t=0`. For Asian this means averaging over
the future monitoring dates only (the standard convention). For lookback and
barrier, where the running extremum/breach state must include the starting
spot (matching Phase 1's analytic treatment, which prices at inception with
the extremum already "at" spot), each payoff type seeds its own running
state from `spot` in its constructor, before any `observe()` call.

**This does not change Phase 2's public API.** `monte_carlo_european` keeps
its exact existing signature. Internally, I'll add a private single-step
template helper shared between it and the new `monte_carlo_digital`, so the
identical single-draw loop isn't duplicated — but nothing Phase 2 exposes
changes shape. If you'd rather I leave `monte_carlo_european` fully
untouched, including internally, I can keep the tiny duplication instead;
either way no external behavior changes.

## 3. Products

- **Arithmetic Asian** (fixed and floating strike): `AsianPayoff` tracks
  running sum and count; average = sum/count.
- **Geometric Asian** (fixed and floating strike): tracks running sum of
  `log(price)`; geometric average = `exp(sum/count)`. This is also the
  control variate for arithmetic Asian (§4).
- **Barrier**, all eight types: `BarrierPayoff` tracks a `breached` flag,
  updated by comparing each observed price against the barrier level in the
  configured direction. At `result()`: knock-out pays the vanilla payoff if
  never breached, else the rebate (immediately in reality, but since this
  pricer discounts once at the end, the rebate is treated as paid at
  expiry for a knock-out — a simplification I'm flagging, see §7); knock-in
  is the mirror.
- **Lookback**, fixed and floating strike: `LookbackFixedPayoff` /
  `LookbackFloatingPayoff` track running min and/or max, seeded from spot.
- **Digital**: `DigitalPayoff` implements the simple `Payoff` concept
  (terminal-spot only) from Phase 2 — no new architecture needed, it's
  priced through the (internally shared, externally unchanged) single-step
  path.

## 4. Variance reduction

- **Antithetic variates**: a pricer-level `bool antithetic` flag. When set,
  each path also runs the mirrored draws (`-z` at every step, computed
  in-place, no second RNG call needed since Philox is a pure function and
  negation is free) through a second fresh payoff instance, and the pair's
  *average* payoff is the single sample fed to the Welford accumulator. This
  is the statistically correct way to do it — averaging within the pair
  before accumulating is what makes the reported standard error valid;
  treating both correlated draws as independent samples would understate
  the true variance.
- **Control variate** (geometric Asian for arithmetic Asian): on the same
  path, evaluate both the arithmetic and geometric Asian payoffs (same
  draws), and accumulate
  `arithmetic_payoff - (geometric_payoff - geometric_analytic_price)`
  using a fixed coefficient of 1 (CLAUDE.md's locked design only specifies
  the control pairing, not a coefficient-estimation scheme; a fixed
  coefficient of 1 is the standard textbook choice for this specific pair
  precisely because arithmetic and geometric Asian payoffs are extremely
  highly correlated, and it keeps the estimator single-pass/streaming rather
  than needing a pilot run to estimate an optimal coefficient). `Var(payoff -
  1*(geometric - geometric_analytic))` is unbiased since
  `E[geometric_payoff] = geometric_analytic_price` under the same discounting
  — the analytic price is Phase 1's `kemna_vorst`.
- **Brownian-bridge continuity correction** (discretely-monitored barriers):
  between two consecutive observed prices `S_t, S_{t+dt}` that don't
  themselves cross the barrier, the *continuous* path between them still
  might have. Conditional on both endpoints, that's a Brownian bridge, and
  the probability it touched a level `H` has a closed form:
  `p_cross = exp(-2*(H-S_t)*(H-S_{t+dt)) / (vol^2 * S_avg^2 * dt))` (in
  log-price space, using the standard Brownian-bridge hitting-probability
  result). I'm implementing this as a **probabilistic correction**: draw one
  extra uniform per step (via the same Philox counter scheme, a third
  "stream" — see open question in §7) and treat the step as breached if that
  uniform falls under `p_cross`, in addition to the discrete endpoint check.
  This directly reduces the bias from under-detecting barrier touches at
  coarse monitoring, which is the CLAUDE.md-specified goal ("reduces
  discrete-monitoring bias; report before/after bias").

## 5. Test plan

Mapped directly to CLAUDE.md §6 Phase 3's required tests:

- Geometric Asian MC within 3 SE of `kemna_vorst` (Phase 1 oracle).
- Barrier MC within 3 SE of `reiner_rubinstein` as `monitoring_points`
  increases; convergence asserted over ≥ 4 values (e.g. 12, 50, 200, 800).
- Lookback MC within 3 SE of `goldman_sosin_gatto`-equivalent Phase 1
  functions (`lookback_fixed_strike`, `lookback_floating_strike`).
- In+out parity: `knock_in + knock_out == vanilla` within combined SE, all
  four direction/type pairs — mirrors the exact identity already used as a
  Phase 1 analytic oracle, now at the MC level.
- Digital MC call ≤ vanilla MC call spread bound.
- Arithmetic Asian MC value ≤ corresponding European MC value.
- Antithetic variance reduction factor measured and reported for every
  product (ratio of plain-MC variance to antithetic variance at matched path
  count).
- Control variate variance reduction factor for arithmetic Asian, asserted
  ≥ 5x, reported.
- Brownian-bridge before/after bias against the continuous Reiner-Rubinstein
  price, reported.
- Log-log RMSE-vs-paths convergence plot (European, as the cleanest case)
  with fitted slope asserted at `-0.5 ± 0.05`.

## 6. Documentation deliverables

- `docs/validation-report.md`: VR factor table (product × technique), the
  convergence plot, and the Brownian-bridge before/after bias numbers — all
  from actually-executed runs in this session, per CLAUDE.md §2.2.
- Since this is a C++-only phase (no plotting library — Python bindings are
  Phase 6, and §5's permitted-dependency list doesn't include a plotting
  library), the "convergence plot" will be real measured `(path_count, RMSE)`
  data points rendered as a hand-written SVG built from those actual
  numbers, embedded in the validation report — not a placeholder, not a
  Python-generated chart I don't have the tooling for yet.

## 7. Open questions for you

1. **Rebate timing simplification** (§3, barrier): paying the rebate at
   expiry (single discount factor) rather than at the moment of breach is a
   simplification vs. a fully realistic barrier. It doesn't affect any of
   the required tests (all use `rebate=0`), but it means the pricer isn't
   literally correct for a nonzero rebate yet. Fine to ship with this
   documented as a known simplification, or do you want immediate-payment
   rebate timing now (requires tracking *when* the breach happened, not just
   whether)?
2. **Brownian-bridge extra randomness stream**: the crossing-probability
   correction needs one more uniform draw per step beyond the normal used to
   advance the price. I'm planning to reuse the existing counter scheme with
   a distinguishing marker in the unused word 3 of the Philox counter
   (`{path_lo, path_hi, step, 1}` for this stream vs. `{path_lo, path_hi,
   step, 0}` for the price-advancing normal) — this is exactly what word 3
   was left reserved for in Phase 2. Confirm this is the right use of that
   reserved word, since (like the Phase 2 counter layout) this becomes a
   permanent commitment once used.
3. **Internal sharing between `monte_carlo_european` and the new
   `monte_carlo_digital`** (§2): share a private single-step template helper
   (my preference, avoids duplicating the loop), or keep them fully
   independent with some duplication? Doesn't affect any public API either
   way.

I'd like your read on these three before I start implementing — #2
especially, since it's another RNG-scheme commitment like Phase 2's counter
layout.
