# Phase 1 — Analytic Layer and CFA Invariants

Status: **implemented, Phase 1 gate passed**

## 1. Purpose

Implement closed-form analytic pricers that will serve two roles for the rest
of the project: standalone, independently-testable pricing functions, and the
validation oracles that every later Monte Carlo pricer (Phase 2+) is checked
against. Also implement the CFA Level I Derivatives invariant test suite and
`docs/cfa-mapping.md`, per CLAUDE.md §6 Phase 1.

No Monte Carlo, RNG, or threading code is in scope here — that is Phase 2.

## 2. Requirements

### 2.1 Functional — pricers

All formulas below are standard, public-domain option-pricing mathematics
(Black–Scholes 1973, Merton 1973, Black 1976, Kemna–Vorst 1984,
Reiner–Rubinstein 1991, Goldman–Sosin–Gatto 1979, Cox–Ross–Rubinstein 1979),
described here in my own words and notation. Nothing is copied from CFA
curriculum text; module references in `docs/cfa-mapping.md` are numbers/topics
only, per the non-negotiable constraint in CLAUDE.md §2.3.

- Black–Scholes–Merton European call/put with continuous carry yield `q`
- Black-76 (options on futures/forwards)
- Kemna–Vorst continuously-monitored geometric-average Asian, fixed strike
- Reiner–Rubinstein continuous barrier, all eight types: `{up, down} ×
  {in, out} × {call, put}`
- Goldman–Sosin–Gatto lookback, both fixed-strike and floating-strike, call
  and put
- Digital / binary option: cash-or-nothing and asset-or-nothing, call and put
- Forward price under cost of carry, and forward contract value during its
  life
- Futures price at inception (equals the forward price under deterministic
  rates) and mark-to-market value between settlements
- Cox–Ross–Rubinstein binomial tree, one-period and *n*-period, exposing the
  risk-neutral probability explicitly

### 2.2 Functional — CFA invariants

`tests/cfa_invariants_test.cpp` implementing every row of the table in
CLAUDE.md §6 Phase 1 (cost of carry, forward value at initiation/during life,
futures/forward equivalence, exercise value + time value, six factor-sensitivity
signs, put–call parity, put–call forward parity, binomial risk-neutrality,
binomial→BSM convergence, no-arbitrage bounds, monotonicity), and
`docs/cfa-mapping.md` mapping each to a learning-module number.

### 2.3 Non-functional

- `double` precision throughout, no third-party math dependency (constraint
  §5 — the normal CDF, PDF, and every pricing formula are hand-written; only
  `std::erf` from the C++ standard library is used as the elementary function
  building block, same tier as `std::exp`/`std::log`, not a "dependency").
- Every pricer is a pure function: no shared/global state, no allocation
  beyond simple locals, `noexcept` where the math cannot throw.
- Per constraint §2.5, every pricer must have an independent test oracle. See
  §5 (test plan) for exactly what oracle each one gets — this is the part I
  most want your review on before I implement, because it determines how
  much I can trust the numbers.

## 3. Repository additions

```
include/mcd/core/normal.hpp       standard_normal_pdf, standard_normal_cdf
src/core/normal.cpp
include/mcd/pricers/analytic.hpp  BSM, Black-76, Kemna-Vorst, Reiner-Rubinstein,
                                   Goldman-Sosin-Gatto, digital, forward/futures
src/pricers/analytic.cpp
include/mcd/pricers/binomial.hpp  CRR binomial
src/pricers/binomial.cpp
tests/analytic_test.cpp
tests/cfa_invariants_test.cpp
docs/cfa-mapping.md               (filled in; skeleton already exists)
```

`include/mcd/core/normal.hpp` is introduced now (ahead of Phase 2, which the
Phase 0 skeleton originally slated it for) because the analytic formulas need
`Φ(x)` and `φ(x)`. Phase 2 will extend this same header with the inverse-CDF
Acklam approximation needed for RNG→normal transform; no restructuring
required later.

## 4. Interfaces

```cpp
// include/mcd/core/types.hpp — additions
namespace mcd {

enum class OptionType { Call, Put };
enum class BarrierDirection { Up, Down };
enum class BarrierKnock { In, Out };
enum class StrikeStyle { Fixed, Floating };
enum class DigitalStyle { CashOrNothing, AssetOrNothing };

} // namespace mcd
```

```cpp
// include/mcd/core/normal.hpp
namespace mcd {
[[nodiscard]] double standard_normal_pdf(double x) noexcept;
[[nodiscard]] double standard_normal_cdf(double x) noexcept;
} // namespace mcd
```

```cpp
// include/mcd/pricers/analytic.hpp
namespace mcd::pricers {

// European vanilla, continuous carry yield q.
[[nodiscard]] double black_scholes_merton(
    double spot, double strike, double rate, double carry_yield,
    double vol, double time, OptionType type) noexcept;

// Options on futures/forwards.
[[nodiscard]] double black76(
    double forward, double strike, double rate, double vol, double time,
    OptionType type) noexcept;

// Continuously-monitored geometric-average Asian, fixed strike.
[[nodiscard]] double kemna_vorst(
    double spot, double strike, double rate, double carry_yield,
    double vol, double time, OptionType type) noexcept;

// Continuous barrier, all eight types via direction x knock x OptionType.
[[nodiscard]] double reiner_rubinstein(
    double spot, double strike, double barrier, double rate,
    double carry_yield, double vol, double time, OptionType type,
    BarrierDirection direction, BarrierKnock knock, double rebate = 0.0) noexcept;

// Lookback, fixed strike: payoff uses realized extremum vs. strike.
[[nodiscard]] double lookback_fixed_strike(
    double spot, double strike, double rate, double carry_yield,
    double vol, double time, OptionType type) noexcept;

// Lookback, floating strike: payoff uses spot vs. realized extremum.
[[nodiscard]] double lookback_floating_strike(
    double spot, double rate, double carry_yield, double vol, double time,
    OptionType type) noexcept;

// Digital / binary.
[[nodiscard]] double digital(
    double spot, double strike, double rate, double carry_yield, double vol,
    double time, OptionType type, DigitalStyle style,
    double cash_amount = 1.0) noexcept;

// Cost-of-carry forward price and value during life.
[[nodiscard]] double forward_price(double spot, double rate, double carry_yield,
                                    double time) noexcept;
[[nodiscard]] double forward_value(double forward_now, double forward_at_inception,
                                    double rate, double time_to_expiry) noexcept;

// Futures: inception price coincides with forward_price() under deterministic
// rates; mark-to-market is the undiscounted daily settlement gain/loss.
[[nodiscard]] double futures_mark_to_market(double futures_price_new,
                                             double futures_price_old) noexcept;

} // namespace mcd::pricers
```

```cpp
// include/mcd/pricers/binomial.hpp
namespace mcd::pricers {

struct BinomialResult {
    double price;
    double risk_neutral_probability;
    double up_factor;
    double down_factor;
};

[[nodiscard]] BinomialResult crr_binomial(
    double spot, double strike, double rate, double carry_yield, double vol,
    double time, int steps, OptionType type) noexcept;

} // namespace mcd::pricers
```

## 5. Algorithms (own-words summary, public-domain math)

- **BSM**: `d1 = [ln(S/K) + (r-q+σ²/2)T] / (σ√T)`, `d2 = d1 - σ√T`. Call =
  `S e^{-qT} Φ(d1) - K e^{-rT} Φ(d2)`; put via `Φ(-d2), Φ(-d1)`.
- **Black-76**: BSM with `S e^{-qT} → F e^{-rT}` and `q → r` in the `d1/d2`
  drift term (standard reduction — a forward already embeds carry).
- **Kemna–Vorst**: the continuous geometric average of GBM is itself
  lognormal with adjusted volatility `σ̂ = σ/√3` and adjusted drift; price via
  BSM with `σ → σ̂` and an adjusted effective carry yield derived from
  matching the first moment of the geometric average.
- **Reiner–Rubinstein**: closed-form piecewise combination of BSM-like terms
  in `S`, `K`, `H` (barrier level) — standard eight-case formula, implemented
  per case rather than one monolithic expression, to keep each case testable
  and to keep McCabe complexity down.
- **Goldman–Sosin–Gatto**: closed-form in terms of `Φ` evaluated at several
  arguments involving `ln(S/extremum)`, reflecting the reflection principle
  for the running extremum of GBM.
- **Digital**: cash-or-nothing = `e^{-rT} Φ(±d2)` (times `cash_amount`);
  asset-or-nothing = `S e^{-qT} Φ(±d1)`.
- **CRR binomial**: `u = e^{σ√Δt}`, `d = 1/u`,
  `π = (e^{(r-q)Δt} - d)/(u-d)`, backward induction with early-exercise check
  disabled here (European only — American is Phase 5/LSM). `π` is returned
  explicitly per the invariant-table requirement.

## 6. Test plan — this is the section I want scrutinized

Constraint §2.5 requires every pricer to have an independent oracle. Here is
exactly what oracle each formula gets. I'm flagging explicitly where I'm
relying on a mathematical identity (provably exact, no memorization risk)
versus a memorized published numeric example (where I could be wrong), so we
can decide together whether the latter needs firmer sourcing before I write
it into a test.

**Identity-based oracles (exact, zero memorization risk — preferred):**

- Put–call parity (`C + Ke^{-rT} = P + Se^{-qT}`) and put–call forward parity
  — required anyway by the invariant table, and also validates BSM/Black-76
  internally to 1e-12.
- CRR binomial → BSM convergence as `n → ∞` — required by the invariant
  table; this is a genuine cross-derivation (discrete tree vs. closed-form
  integral), not circular.
- Black-76 ≡ BSM when the forward is substituted for a carry-adjusted spot
  (`F = S e^{(r-q)T}` and setting `q_BSM = r`) — algebraically exact, checked
  to 1e-9.
- Digital decomposition: `vanilla_call = asset_or_nothing_call - K ×
  cash_or_nothing_call`, and the put-side mirror. Exact identity from
  splitting the vanilla payoff `(S-K)⁺ = S·1_{S>K} - K·1_{S>K}`.
- Barrier in + out = vanilla (same `S,K,H,r,q,σ,T`, zero rebate), for all
  four direction/type pairs — exact identity, and it's explicitly endorsed
  as a "limiting case" style check under constraint §2.5. (This mirrors a
  Phase 3 requirement but there's no reason not to hold the analytic
  formulas to it now too.)
- No-arbitrage bound and monotonicity checks from the invariant table (swept
  grids, sign/direction assertions only — no magic numbers).
- Lookback ordering bounds: fixed-strike lookback call ≥ vanilla call at the
  same strike (running max ≥ terminal spot, pathwise, so the closed forms
  must preserve that ordering); analogous put-side and floating-strike
  bounds.

**Memorized textbook reference values (flagging for your review):**

- One classic, widely-republished BSM example
  (`S=42, K=40, r=10%, σ=20%, T=0.5, q=0`, giving call ≈ 4.76 / put ≈ 0.81 by
  my own hand recomputation this session) — used as a single sanity-check
  test with a deliberately loose tolerance (5e-2), *not* as the primary
  oracle. I'm confident in this one because it's extremely widely cited
  (Hull's textbook, and countless independent restatements of it), and I
  recomputed it by hand above rather than trusting pure recall.
- I do **not** plan to hardcode remembered numeric reference values for the
  barrier, lookback, or digital formulas — I'm not confident enough in
  specific digits from memory, and CLAUDE.md's "never fabricate a number"
  principle should extend to test fixtures, not just benchmark reports. For
  those three, the identity-based oracles above are the actual validation;
  if you have (or want me to find) a specific published reference table for
  barriers/lookbacks you trust, I'll add tight-tolerance tests against it as
  a second layer — tell me and I'll wire it in before or after this phase
  gate, your call.

**Additional required tests:**

- CRR one-period risk-neutral probability formula checked symbolically
  (`π = (e^{(r-q)Δt}-d)/(u-d)`) and confirmed the *price* is independent of
  the real-world drift parameter (there is no real-world drift parameter in
  this pricer's signature at all — the test instead confirms price depends
  only on `r, q, σ`, not on any assumed real growth rate, by construction).
- Binomial convergence error < 0.01 at `n=5000`; monotonically decreasing
  error over `n ∈ {10, 50, 250, 1000, 5000}` — I'll pick `S, K` for this
  specific test such that the well-known odd/even CRR oscillation doesn't
  violate monotonicity at these particular sample points (verified
  empirically when I run it, not assumed; if plain CRR doesn't cooperate at
  these exact n values I'll report back rather than silently cherry-picking
  parameters until it passes).
- Forward/futures: `V₀=0` at the fair forward price; `Vₜ` formula: exact
  algebraic identity check; futures/forward equivalence under deterministic
  rates: exact (they're the same formula in this model).

## 7. Acceptance criteria

1. All Phase 1 pricers implemented per §4 interfaces, building warning-free
   under the existing `-Wall -Wextra -Wpedantic -Wshadow -Wconversion -Werror`.
2. `tests/analytic_test.cpp` green: every identity-based oracle in §6 passes;
   the one Hull sanity-check value passes within its stated tolerance.
3. `tests/cfa_invariants_test.cpp` green: every row of CLAUDE.md's Phase 1
   invariant table has a passing assertion.
4. `docs/cfa-mapping.md` filled in, module-number references only, no
   curriculum text.
5. CI green on the full matrix (all 7 build/test jobs + clang-tidy), same
   bar as Phase 0.
6. No forbidden compiler flags introduced.

## 8. Open question for you

Barrier/lookback/digital reference-value sourcing (§6 above): proceed with
identity-only validation for those three families, or do you have/want a
specific published source I should use for tighter numeric tests? I can
proceed with identity-only as the default if I don't hear otherwise, since
it's a real, non-circular oracle — just a looser one than a magic-number
match.
