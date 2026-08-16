# Phase 0 — Specification and Scaffold

Status: **proposed, awaiting approval**

## 1. Purpose

Establish the repository skeleton, build system, toolchain configuration, and CI
pipeline that every later phase builds on. Phase 0 produces no pricing logic —
only the scaffold and a single trivial test proving the toolchain works end to
end. This follows CLAUDE.md §6 Phase 0 and §3 (spec-driven development: this
document first, stop for approval, then tests, then implementation).

## 2. Requirements

### 2.1 Functional

- A CMake project named `mcd`, C++20, buildable with both GCC 13+ and Clang 16+.
- Four CMake presets: `debug`, `release`, `asan`, `ubsan`, and a fifth, `tsan`
  (CLAUDE.md §6 Phase 0 lists `debug, release, asan, ubsan, tsan` — five presets,
  not four; corrected here).
- GoogleTest and Google Benchmark fetched via `FetchContent` only — no system
  package dependency, no vendored copies.
- A `.clang-format` (LLVM base style, 100 column limit) and a `.clang-tidy`
  enabling at minimum: `bugprone-*`, `cppcoreguidelines-*` (pragmatic subset),
  `performance-*`, `modernize-*`, with justified suppressions documented inline
  where the coreguidelines subset conflicts with hand-written low-level code
  (e.g. the future Philox implementation's explicit bit manipulation).
- A `.gitignore` covering build directories, compiler caches, IDE metadata
  (`.vscode/` per the non-negotiable constraint in §2.7 of CLAUDE.md), and OS
  cruft.
- A GitHub Actions workflow at `.github/workflows/ci.yml` with a matrix of
  `{gcc, clang} × {Debug, Release}` (4 jobs) plus three additional jobs for
  ASan, UBSan, and TSan builds/test-runs (7 jobs total). CLAUDE.md's phrase
  "matrix: {GCC, Clang} × {Debug, Release}, plus ASan/UBSan/TSan jobs" and its
  gate "CI green on all six matrix jobs" are inconsistent (4 + 3 = 7, not 6);
  resolved by decision — 7 jobs, gate text corrected to "seven" throughout
  this project's docs.
- One trivial GoogleTest case (e.g. asserting a placeholder `mcd::version()`
  string is non-empty) proving the test binary builds, links, and runs under
  every configuration in the matrix.
- Compiler warnings `-Wall -Wextra -Wpedantic -Wshadow -Wconversion`, treated as
  errors in CI (not necessarily in local `debug` builds, to keep local
  iteration friction low — open question below).
- Release configuration flags exactly `-O3 -march=native -DNDEBUG`, no
  `-ffast-math` / `-Ofast` / `-funsafe-math-optimizations` anywhere, ever.

### 2.2 Non-functional

- No implementation logic beyond what is needed to prove the scaffold: no
  pricers, no RNG, no math. Those begin in Phase 1/2.
- Repository must build from a clean clone with no manual steps beyond
  `cmake --preset <name> && cmake --build --preset <name> && ctest --preset <name>`.

## 3. Repository layout

Per CLAUDE.md §6 Phase 0, created as empty-but-tracked directories (via
`.gitkeep` where nothing else populates them yet) or left uncreated until a
later phase actually needs them — this document proposes creating the full
tree now so later phases only add files, never restructure:

```
mcd/
├── CMakeLists.txt
├── CMakePresets.json
├── CLAUDE.md
├── README.md
├── .clang-format
├── .clang-tidy
├── .gitignore
├── .github/workflows/ci.yml
├── docs/
│   ├── design/00-requirements.md   (this file)
│   ├── validation-report.md        (skeleton, populated from Phase 3 on)
│   ├── cfa-mapping.md              (skeleton, populated in Phase 1)
│   └── benchmarks/                 (empty; first artifacts in Phase 2)
├── include/mcd/
│   ├── core/      types.hpp rng.hpp normal.hpp stats.hpp thread_pool.hpp
│   ├── models/    gbm.hpp
│   ├── payoffs/   european.hpp asian.hpp barrier.hpp lookback.hpp digital.hpp
│   ├── pricers/   analytic.hpp binomial.hpp monte_carlo.hpp lsm.hpp
│   └── greeks/    finite_difference.hpp
├── src/
├── apps/mcd_cli/
├── tests/
├── bench/
├── bindings/python/
├── web/
└── infra/
```

Phase 0 populates: root files, `.github/workflows/ci.yml`, `docs/` skeleton
files (empty section headers only, no content requiring later-phase data),
`include/mcd/core/types.hpp` as a placeholder with only a `namespace mcd`
and a `version()` declaration, its `.cpp` in `src/`, and one test in
`tests/`. All other headers listed above are **not** created in Phase 0 — they
are named here for reference but appear only when the phase that needs them
begins (Phase 1 for `pricers/analytic.hpp`, Phase 2 for `core/rng.hpp` etc.).
Creating empty stub headers now would violate "don't add features beyond what
the task requires."

## 4. Interfaces

Phase 0's only public interface:

```cpp
// include/mcd/core/types.hpp
namespace mcd {
    [[nodiscard]] std::string_view version() noexcept;
}
```

No other interface is in scope. Numerics, RNG, and pricer interfaces are
specified in their own phase's design document.

## 5. Test plan

- `tests/version_test.cpp`: `EXPECT_FALSE(mcd::version().empty())`.
- CI executes this test under all matrix configurations via `ctest`.
- ASan/UBSan/TSan jobs run the same single test under their respective
  sanitizer build — trivial at this stage, but proves the sanitizer presets
  themselves are correctly wired before any real code depends on them.

## 6. Acceptance criteria

1. `cmake --preset debug`, `release`, `asan`, `ubsan`, `tsan` each configure
   without error on a clean clone, on both GCC 13+ and Clang 16+.
2. Each preset builds without warnings under
   `-Wall -Wextra -Wpedantic -Wshadow -Wconversion`.
3. `ctest` passes (1/1 test) under every preset.
4. GitHub Actions workflow is green on every job in the matrix.
5. `.clang-format` and `.clang-tidy` are present and `clang-tidy` runs clean
   against the Phase 0 source files in CI.
6. No forbidden compiler flag (`-ffast-math`, `-Ofast`,
   `-funsafe-math-optimizations`) appears anywhere in the build configuration.
7. `.vscode/` and all build directories are git-ignored and not committed.

## 6a. Implementation note discovered during build verification

Google Benchmark is only fetched under the `debug` and `release` presets
(`MCD_ENABLE_BENCHMARKS` defaults `ON` there, `OFF` under `asan`/`ubsan`/`tsan`).
Cause: Benchmark's own `CMakeLists.txt` runs a configure-time `try_run` to pick
a regex backend, and that probe inherits the sanitizer flags injected into
`CMAKE_CXX_FLAGS`. A TSan-instrumented probe binary fails to execute on macOS
(missing task-for-pid entitlement for the sanitizer runtime), which makes every
regex-backend check report failure and aborts Benchmark's configure with
`Failed to determine the source files for the regular expression backend`.
Since no benchmark target exists yet (or is needed) under sanitizer presets —
those presets test correctness, not throughput — skipping the fetch there is
correct on its own merits, not just a workaround. GoogleTest is unaffected and
still fetched under every preset.

A second local-machine limitation, since partially resolved: with **Apple
clang** (17, macOS 26.5.2, Apple Silicon), the `asan` preset's binary hangs
indefinitely inside the AddressSanitizer runtime at `FindDynamicShadowStart`
— before any user code runs — and the `tsan` preset's binary segfaults at
startup for the same class of reason (ThreadSanitizer runtime shadow-memory
setup). Both presets **configure and build cleanly** with Apple clang
(compiler and linker accept the sanitizer flags without error); only runtime
*execution* of the resulting binary is affected. This matches a known class
of Apple-clang sanitizer runtime issues on recent macOS/Apple Silicon.

Discovered during Phase 4: **Homebrew GCC 16's** AddressSanitizer runtime does
not have this problem on the same machine — `asan` builds, links, and runs
correctly under GCC 16, giving real local ASan verification for the first
time (used starting Phase 4; see `docs/validation-report.md`). GCC on macOS
ARM64 does not ship a working ThreadSanitizer runtime at all, though (a
missing `___tsan_init` symbol at link time) — this is a genuine platform gap,
not a bug, so `tsan` specifically remains CI-only regardless of toolchain.
The `ubsan` preset is unaffected under either toolchain — it builds and runs
cleanly, tests passing. Authoritative verification of `tsan`, and a second
opinion on `asan`, are still deferred to the Linux CI matrix (GCC 13 / Clang
16 on `ubuntu-24.04`), which is precisely why CLAUDE.md's CI matrix requires
them as separate jobs rather than relying on local developer runs.

## 7. Open questions — resolved / remaining

1. **Matrix job count — resolved.** 7 jobs: `{GCC, Clang} × {Debug, Release}`
   plus one job each for ASan, UBSan, TSan. The Phase 0 gate's "six" is a typo
   in CLAUDE.md and is corrected to "seven" wherever this project restates it.
2. **Warnings-as-errors scope.** CLAUDE.md says "warnings-as-errors in CI."
   Should local `debug`/`release` presets also treat warnings as errors, or
   only the CI-invoked configuration?
3. **`-march=native` in CI.** `-march=native` bakes in the build machine's
   ISA. GitHub-hosted runners are heterogeneous across runs, which is fine for
   correctness testing but means Release-preset CI binaries aren't portable
   and aren't representative of a fixed benchmark machine. Confirm this is
   acceptable for CI (benchmarks in later phases will need a controlled,
   reported machine anyway per the non-negotiable "never fabricate a
   benchmark number" rule) — i.e. CI's Release job is a correctness check, not
   a benchmark run.
4. **clang-tidy strictness.** Do you want a specific curated check list now,
   or should Phase 0 ship a conservative default (`bugprone-*, performance-*,
   modernize-*, cppcoreguidelines-*` with a short suppression list) that gets
   tightened later if it proves too noisy?

I'll proceed once you confirm the layout above and answer (or explicitly
defer) the open questions.
