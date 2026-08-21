# Stretch Goal 4 — SIMD (vectorised Philox and inverse CDF)

Status: **implemented, Stretch Goal 4 gate passed** (NEON target
confirmed; `simd_enabled` flag surfaced in `mcd_cli`/bindings/Lambda
output rather than engine-only)

## 1. Purpose

Per CLAUDE.md §7 item 4: "SIMD — vectorised Philox and inverse CDF, with
honest before/after numbers." Scope is deliberately narrow — CLAUDE.md
names exactly these two primitives, not the whole pricing loop. This
matches the architecture cleanly: `standard_normal_variate` (Philox +
uniform-from-bits + inverse CDF) is the one function every pricer in this
engine calls once per path per step, so accelerating it accelerates
everything without touching payoff logic, accumulation, or the thread
pool at all.

## 2. The instruction-set fork — resolved, not deferred

CLAUDE.md §3 flags any change touching the RNG scheme as requiring
explicit approval, and the informal framing of this stretch goal
elsewhere in the project's history assumed AVX2/AVX-512. That assumption
doesn't hold here: **this development machine is Apple Silicon
(ARM64)**, which cannot compile or run AVX2/AVX-512 intrinsics at all —
verifying an AVX2 path would require a Linux/x86 container for every
build and every benchmark, with no way to cross-check the numbers
against real local hardware.

**Proposed resolution: target ARM NEON (Advanced SIMD), not AVX2.**
Two independent reasons, not just "it's what's available":

1. **It matches the actual production target.** The AWS Lambda deployed
   in Phase 7 already runs on ARM64/Graviton (`docs/design/07-aws-demo.md`
   §5: "Compute: Lambda container image, ARM64/Graviton"). NEON
   vectorization is directly usable in the deployed demo; AVX2 would
   never run there.
2. **It's verifiable exactly the way every other benchmark in this
   project already is.** Every existing number in
   `docs/validation-report.md` is disclosed as measured on "Apple M3
   Pro" — NEON keeps that same honesty property; an AVX2 path built and
   only ever run inside a container would be the first benchmark in this
   project not measured on the machine doing the work.

CI (`ubuntu-24.04`, x86_64 runners) cannot exercise the NEON path either
way. The implementation is feature-gated
(`#if defined(__ARM_NEON) || defined(__aarch64__)`) so x86 builds compile
and run the portable scalar fallback — CI stays green, exercising
correctness of the fallback path, same as it always has. The NEON path
itself is verified and benchmarked locally, disclosed as such.

## 3. What gets vectorised, and the batch-width mismatch

`standard_normal_variate(seed, path_index, draw_index)` has two steps:

1. **Philox4x32-10** operates on four `uint32_t` counter words per call.
   A 128-bit NEON register (`uint32x4_t`) holds exactly four lanes — so
   vectorising *across paths* (not across the counter's own four words)
   packs **4 independent path indices' counters into one register per
   word position** (structure-of-arrays: one `uint32x4_t` for word 0
   across 4 paths, one for word 1, etc.), and runs all 10 Philox rounds
   as 4-wide integer SIMD ops. `mul_hi_lo`'s 32×32→64 multiply becomes
   `vmull_u32` (widening multiply, exact by construction — no rounding
   exists for integer multiplication).
2. **Acklam's inverse CDF** operates on `double`. A 128-bit NEON register
   (`float64x2_t`) holds only **two** lanes. So one 4-wide Philox call
   produces 4 uniforms, consumed as **two** 2-wide inverse-CDF calls —
   the batch API is `standard_normal_variate_batch4`, internally 1 Philox
   SIMD op + 2 inverse-CDF SIMD ops, not a uniform 4-wide pipeline
   end-to-end. This asymmetry is inherent to the hardware (32-bit vs.
   64-bit lane width), not a design choice.

```cpp
// include/mcd/core/rng_simd.hpp
namespace mcd {

constexpr bool kHasNeon =
#if defined(__ARM_NEON) || defined(__aarch64__)
    true;
#else
    false;
#endif

// Computes standard_normal_variate(seed, path_index_base + i, draw_index) for
// i in 0..3, using NEON where available. Falls back to 4 scalar calls when
// kHasNeon is false -- callers do not need to branch on availability.
// MUST be bitwise-identical to 4 individual standard_normal_variate calls
// (sec.5); this is a hard correctness requirement, not a "close enough"
// numerical approximation.
[[nodiscard]] std::array<double, 4> standard_normal_variate_batch4(
    std::uint64_t seed, std::uint64_t path_index_base, std::uint32_t draw_index = 0) noexcept;

} // namespace mcd
```

## 4. The determinism story — cleaner than it first looks

Two different kinds of "determinism" are at stake, and they need separate
treatment:

- **Philox is pure integer arithmetic** (add, multiply, xor, shift). IEEE
  754 does not apply; there is no rounding, no reassociation, no
  fused-multiply-add ambiguity. A SIMD widening multiply and four scalar
  multiplies compute the *identical* 64-bit products, always, on any
  conforming compiler. Vectorising Philox cannot introduce
  nondeterminism — this is a mathematical guarantee, not something that
  needs to be tested to be trusted (though sec.5 tests it anyway).
- **The inverse CDF is floating-point** (polynomial evaluation, `sqrt`,
  `exp`, `erfc` via `standard_normal_cdf`). Here the real risk is
  **FP contraction**: the compiler is permitted by default (outside
  `-ffast-math`, which CLAUDE.md already forbids for unrelated reasons)
  to fuse a `a * b + c` sequence into a single fused-multiply-add
  instruction, which rounds once instead of twice and can differ in the
  last bit from the unfused sequence. If the scalar path and the NEON
  path get contracted differently, they will not be bitwise identical.
  **Resolved by disabling contraction explicitly** for the translation
  unit containing both implementations
  (`#pragma STDC FP_CONTRACT OFF`, backed by `-ffp-contract=off` in
  `CMakeLists.txt` for that one file) — a narrow, explicit,
  single-purpose flag, unrelated to `-ffast-math`'s much larger set of
  unsafe relaxations (no-NaN, no-signed-zero, reciprocal approximation,
  etc.), and consistent with CLAUDE.md §2's rejection of the latter.

## 5. Test plan

- **Bitwise identity, Philox path**: `standard_normal_variate_batch4`
  vs. four scalar `standard_normal_variate` calls, compared via
  `std::bit_cast<uint64_t>` equality (not `EXPECT_DOUBLE_EQ`), across a
  large grid of `(seed, path_index_base, draw_index)` including edge
  cases (`path_index_base` near `2^32` boundary, since word 0/1 of the
  counter split there).
- **Bitwise identity holds under both presets**: run the above under
  `debug` and `release` (`-O3 -march=native`) — contraction risk is
  highest under `-O3`, so this is the binding case, not a formality.
- **Fallback correctness on non-ARM**: the `#else` scalar branch is
  exercised by every existing test in the suite (it's the only branch
  that runs in CI); no separate test needed beyond confirming the
  feature-gate macro compiles both ways (checked locally by forcing
  `kHasNeon = false` and rebuilding once).
- **Throughput, honestly reported**: Google Benchmark comparison,
  `BM_StandardNormalVariateScalar` vs. `BM_StandardNormalVariateBatch4`,
  and the full `BM_MonteCarloEuropean` pipeline with/without the batch
  path wired into `accumulate_paths`'s inner loop — real measured
  paths/sec on this machine, reported whichever way it comes out, not
  assumed in advance. If the speedup is small (RNG may already be a
  minority of total pricing-loop time next to payoff evaluation and
  Welford accumulation), that gets reported too, per CLAUDE.md §2's
  "never fabricate a benchmark number."

## 6. Integration point

`accumulate_paths` in `src/pricers/monte_carlo.cpp` currently calls
`standard_normal_variate` once per path in its inner loop. The batch path
is wired in as an *optional* fast path used only when the remaining
chunk has at least 4 paths left (tail paths fall back to scalar) —
mirrors the LSM design's existing pattern of a documented, narrow
optimisation exception rather than restructuring the general loop.
Behind a `McOptions`-level or compile-time toggle, defaulting to **on**
when `kHasNeon` — off has no effect beyond speed, so there's no
correctness reason to default it off.

## 7. What stays out of scope

- No vectorisation of payoff evaluation, Welford accumulation, or the
  thread pool — CLAUDE.md names Philox and the inverse CDF specifically.
- No AVX2/AVX-512 path (sec.2).
- No change to the public pricer API — `standard_normal_variate` keeps
  its existing signature and behaviour exactly; the batch function is
  additive.

## 8. Open questions for you

1. **NEON over AVX2, for the reasons in sec.2** — confirm, or say if you
   want an x86/AVX2 path pursued anyway via a Linux container despite
   the local-verification gap that implies.
2. **Exposure**: this is a pure internal performance optimisation with no
   new observable behavior (same prices, same determinism guarantee,
   just faster) — propose engine + benchmark + test only, no new
   `mcd_cli`/bindings/AWS surface (there's nothing new to expose; a
   caller already gets the speedup transparently through the existing
   pricers). Confirm, or say if you want an explicit
   `simd_enabled: true/false` field surfaced in `mcd_cli`'s benchmark
   output for transparency.
