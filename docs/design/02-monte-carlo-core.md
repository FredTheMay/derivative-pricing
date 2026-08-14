# Phase 2 — Single-Threaded Monte Carlo Core

Status: **implemented, Phase 2 gate passed**

## 1. Purpose

Implement the counter-based RNG, the inverse-normal transform, the Welford
accumulator, the streaming GBM path generator, and a payoff concept, then
price European options via Monte Carlo and validate against Phase 1's BSM.
Single-threaded only — the thread pool and parallel determinism are Phase 4.

This phase locks in the **path-index ↔ random-stream mapping**, which is the
foundation of the bitwise-determinism guarantee Phase 4 will test. Per
CLAUDE.md §3, any change to the RNG scheme requires your approval, so §4
below is the part I most want you to look at closely.

## 2. Sourcing — verified against primary references, not memory

Per the same principle applied in Phase 1 (never assert a "reference value"
I'm not confident in), I fetched and cross-verified two things this session
rather than trusting recall of exact constants:

- **Philox4x32-10 known-answer test vectors**, from the official Random123
  repository's `tests/kat_vectors`
  (`github.com/DEShawResearch/random123`), fetched and independently
  re-verified with a second raw `curl`:
  ```
  philox4x32 10 00000000 00000000 00000000 00000000 00000000 00000000   6627e8d5 e169c58d bc57ac4c 9b00dbd8
  philox4x32 10 ffffffff ffffffff ffffffff ffffffff ffffffff ffffffff   408f276d 41c83b0e a20bc7c6 6d5451fd
  philox4x32 10 243f6a88 85a308d3 13198a2e 03707344 a4093822 299f31d0   d16cfe09 94fdcceb 5001e420 24126ea1
  ```
  (format: `counter[4] key[2]` → `output[4]`, all hex uint32). These become
  the exact test fixtures in `tests/rng_test.cpp`.
- **Acklam's inverse-normal-CDF coefficients and algorithm structure**, from
  QuantLib's `InverseCumulativeNormal` (`ql/math/distributions/
  normaldistribution.{hpp,cpp}`), which documents itself as implementing
  "Acklam's approximation... by Peter J. Acklam" and includes the exact
  Halley refinement step CLAUDE.md's spec calls for. I'm implementing this
  from the published mathematical algorithm and its constants (public-domain
  numerical method, not copyrightable expression — same footing as the BSM
  formula in Phase 1) in my own code, not copying QuantLib's C++. Verified
  coefficients (`curl`-fetched twice, matched):
  ```
  a1=-3.969683028665376e+01  a2=2.209460984245205e+02  a3=-2.759285104469687e+02
  a4=1.383577518672690e+02   a5=-3.066479806614716e+01  a6=2.506628277459239e+00
  b1=-5.447609879822406e+01  b2=1.615858368580409e+02   b3=-1.556989798598866e+02
  b4=6.680131188771972e+01   b5=-1.328068155288572e+01
  c1=-7.784894002430293e-03  c2=-3.223964580411365e-01  c3=-2.400758277161838e+00
  c4=-2.549732539343734e+00  c5=4.374664141464968e+00   c6=2.938163982698783e+00
  d1=7.784695709041462e-03   d2=3.224671290700398e-01   d3=2.445134137142996e+00
  d4=3.754408661907416e+00
  x_low=0.02425, x_high=1-x_low
  ```

## 3. Requirements

- Philox4x32-10: counter-based, stateless, pure function `(counter, key) →
  4×uint32`.
- Inverse standard normal CDF: Acklam's rational approximation (central
  region + two tail regions) refined by one Halley step.
- Welford accumulator: one-pass mean/variance, exposing standard error.
- GBM terminal-spot generator: exact log-Euler closed form (not an SDE
  discretization), streaming — one normal draw in, one terminal spot out, no
  path storage.
- A `Payoff` concept and a `EuropeanPayoff` implementation.
- `monte_carlo_european(...)`: single-threaded MC pricer for European
  call/put, zero heap allocation in the pricing loop.
- Benchmark: paths/second for European MC at 10⁶ paths, single-threaded.

## 4. The path-index ↔ random-stream mapping (please review)

```
seed: uint64_t  →  Key = {uint32(seed), uint32(seed >> 32)}
(path_index: uint64_t, draw_index: uint32_t = 0)
    →  Counter = {uint32(path_index), uint32(path_index >> 32), draw_index, 0}
```

`philox4x32_10(counter, key)` is a pure function — same inputs always give
the same 4×uint32 output, regardless of call order, thread, or anything else.
Word 0 of the output is converted to a uniform double and inverse-transformed
to a standard normal. This is what makes "path *i* draws the same numbers
regardless of thread count" true by construction rather than by convention:
there is no mutable RNG state to race on or serialize.

`draw_index` defaults to 0 and is unused by Phase 2 (European needs exactly
one normal per path). It exists now so that Phase 3 (multi-step Asian/
barrier paths) can pass the time-step index without changing the counter
layout, and Phase 5 (bumped Greeks, common random numbers) can rely on the
same path index producing the same base-case draw regardless of what bump is
being evaluated. Word 3 of the counter is reserved/unused for now — I'm not
assigning it a meaning speculatively.

**This is the one thing in this phase that's a real design commitment**:
once Phase 3+ code depends on this exact layout, changing it later is a
breaking change to every stored/replayed seed. If you want a different
split (e.g., more bits for draw_index, or reserving word 3 for something
specific now), tell me before I implement.

## 5. Interfaces

```cpp
// include/mcd/core/rng.hpp
namespace mcd {

using PhiloxCounter = std::array<std::uint32_t, 4>;
using PhiloxKey = std::array<std::uint32_t, 2>;

[[nodiscard]] PhiloxCounter philox4x32_10(PhiloxCounter counter, PhiloxKey key) noexcept;

[[nodiscard]] PhiloxKey make_philox_key(std::uint64_t seed) noexcept;
[[nodiscard]] PhiloxCounter make_philox_counter(std::uint64_t path_index,
                                                 std::uint32_t draw_index = 0) noexcept;

// Composes the above with the inverse-CDF transform (core/normal.hpp) to hand
// back the standard normal draw for a given path/draw index under a seed.
[[nodiscard]] double standard_normal_variate(std::uint64_t seed, std::uint64_t path_index,
                                              std::uint32_t draw_index = 0) noexcept;

} // namespace mcd
```

```cpp
// include/mcd/core/normal.hpp — addition
namespace mcd {
[[nodiscard]] double inverse_standard_normal_cdf(double u) noexcept;
}
```

```cpp
// include/mcd/core/stats.hpp
namespace mcd {
class WelfordAccumulator {
  public:
    void add(double value) noexcept;
    [[nodiscard]] std::uint64_t count() const noexcept;
    [[nodiscard]] double mean() const noexcept;
    [[nodiscard]] double variance() const noexcept;        // sample variance, n-1 denominator
    [[nodiscard]] double standard_error() const noexcept;   // sqrt(variance / n)
  private:
    std::uint64_t count_ = 0;
    double mean_ = 0.0;
    double m2_ = 0.0;
};
} // namespace mcd
```

```cpp
// include/mcd/models/gbm.hpp
namespace mcd::models {
struct GbmParams { double spot, rate, carry_yield, vol, time; };
[[nodiscard]] double gbm_terminal_spot(const GbmParams& params, double standard_normal) noexcept;
} // namespace mcd::models
```

```cpp
// include/mcd/payoffs/european.hpp
namespace mcd::payoffs {
template <typename P>
concept Payoff = requires(const P& p, double terminal_spot) {
    { p(terminal_spot) } -> std::convertible_to<double>;
};

struct EuropeanPayoff {
    double strike;
    OptionType type;
    [[nodiscard]] double operator()(double terminal_spot) const noexcept;
};
static_assert(Payoff<EuropeanPayoff>);
} // namespace mcd::payoffs
```

```cpp
// include/mcd/pricers/monte_carlo.hpp
namespace mcd::pricers {
struct McResult {
    double price;
    double standard_error;
    std::uint64_t path_count;
};

[[nodiscard]] McResult monte_carlo_european(double spot, double strike, double rate,
                                             double carry_yield, double vol, double time,
                                             OptionType type, std::uint64_t path_count,
                                             std::uint64_t seed) noexcept;
} // namespace mcd::pricers
```

## 6. Test plan

- **Philox vectors**: the three verified known-answer vectors from §2, exact
  equality (integer comparison, not floating point).
- **Inverse CDF accuracy**: independent oracle via a completely different
  numerical method — Newton's method root-finding `Φ(z) - u = 0` using the
  already-validated `standard_normal_cdf` (Phase 1, `std::erfc`-based) as
  `Φ`, converged to near machine precision. This is a genuinely different
  code path (root-finding on the standard-library error function) from the
  Acklam rational approximation, so it's a real independent check, not a
  circular one. Swept across `u ∈ (1e-12, 1-1e-12)`, log-spaced near the
  tails; asserts max absolute error < 1e-9 as CLAUDE.md specifies.
- **Moment tests at n=10⁶**: mean, variance, skewness, excess kurtosis
  against their known asymptotic standard errors (`SE(mean)=1/√n`,
  `SE(var)≈√(2/n)`, `SE(skew)≈√(6/n)`, `SE(kurtosis)≈√(24/n)`), asserted
  within a stated multiple of each — the same "assert against a statistical
  tolerance, not a fixed epsilon" philosophy CLAUDE.md mandates for MC prices.
- **Kolmogorov-Smirnov test at n=10⁶** against the standard normal CDF, using
  the standard asymptotic critical value for the Kolmogorov distribution.
- **Welford vs. naive two-pass**: 10⁶ samples, agreement to 1e-12.
- **European MC vs. BSM**: ≥ 20 `(S, X, σ, T, r, q)` combinations, fixed
  seed, `|price_mc - price_analytic| < 3 × standard_error_mc`, reporting both
  values on failure per CLAUDE.md's testing philosophy.
- **Determinism**: identical seed ⇒ bitwise-identical price
  (`std::bit_cast<uint64_t>`, integer equality) across two independent runs
  of the same pricer call.
- **Zero allocation**: override global `operator new`/`operator delete` in a
  test fixture with an atomic counter, price 10⁶ paths, assert the counter is
  unchanged.
- **Benchmark**: Google Benchmark case for `monte_carlo_european` at 10⁶
  paths, single-threaded, Release build. Recorded in
  `docs/benchmarks/phase2.md` with actual measured numbers from this
  machine (Apple Silicon Mac — noting explicitly this is a dev-machine
  baseline, not the controlled benchmarking environment Phase 4's scaling
  study will need).

## 7. Acceptance criteria

1. All of §6 passing.
2. Baseline paths/sec recorded in `docs/benchmarks/phase2.md`, real numbers
   only.
3. CI green on the full matrix.
4. Zero heap allocations verified in the pricing loop.
5. No forbidden compiler flags.

## 8. Open question for you

§4's counter/key layout is the one real design commitment in this phase.
Proceed as specified, or do you want changes to the bit layout before I
implement it?
