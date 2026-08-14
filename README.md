# mcd — Monte Carlo Derivatives Pricing & Risk Engine

A C++20 Monte Carlo derivatives pricing and risk engine with closed-form
analytic oracles, a multithreaded core with bitwise-reproducible results,
path-dependent exotics, American options via Longstaff–Schwartz, and
finite-difference Greeks.

**Status: Phase 0 (scaffold) complete.** No pricers exist yet. This README
will grow a results table, scaling chart, and validation methodology section
as each phase in `CLAUDE.md` lands — see that document for the full roadmap
and `docs/validation-report.md` for live status.

## Build

```
cmake --preset debug
cmake --build --preset debug
ctest --preset debug
```

Other presets: `release`, `asan`, `ubsan`, `tsan`.

## Why no `-ffast-math`

`-ffast-math`, `-Ofast`, and `-funsafe-math-optimizations` are never used in
this project. They relax IEEE 754 semantics (reordering, NaN/Inf handling,
signed-zero behaviour) in ways that silently break two things this engine
depends on: bitwise-reproducible Monte Carlo results across thread counts, and
the correctness of the closed-form oracles used to validate every pricer.
Release builds use `-O3 -march=native -DNDEBUG` only.
