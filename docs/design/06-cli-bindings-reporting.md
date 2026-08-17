# Phase 6 — CLI, Python Bindings, and Reporting

Status: **implemented, Phase 6 gate passed**

## 1. Purpose

Per CLAUDE.md §6 Phase 6: a JSON-in/JSON-out CLI (`mcd_cli`) whose every
priced result is a complete, self-describing statistical statement (point
estimate, standard error, 95% CI, path count, seed, elapsed time,
paths/second — never a bare number); pybind11 bindings exposing the pricers,
Greeks, and a benchmark harness, with the GIL released around pricing calls;
a report generator that produces the data-driven parts of
`docs/validation-report.md` from a live run rather than hand-transcription;
and a README rewritten to lead with a real results table.

## 2. Two forks CLAUDE.md doesn't settle — flagging before writing code

### 2.1 JSON without a JSON dependency

CLAUDE.md §5's permitted-dependency list is GoogleTest, Google Benchmark,
pybind11 (Phase 6), and the standard library — nothing else, and §2
constraint 4 says to stop and ask before adding anything beyond it. The
standard library has no JSON support. Options:

- **(a) Hand-write a minimal JSON reader/writer**, scoped exactly to what
  `mcd_cli` needs (flat objects, numbers, strings, bools, one level of
  nesting for arrays of results) — not a general-purpose JSON library. This
  matches the project's existing posture (Philox, the inverse normal CDF,
  Householder QR, and the thread pool are all hand-written primitives
  instead of pulled in) and needs its own correctness tests, same as those.
- **(b) Ask to add a header-only JSON library** (e.g. nlohmann/json) as a
  named exception to §5.

I'm proposing **(a)**, consistent with everything else in this project, and
listing it here rather than silently deciding it because it's the kind of
dependency-boundary call §2 explicitly wants flagged. The hand-written
parser only needs to round-trip the request/response shapes in §3.1 below —
it is not a competitor to a real JSON library and won't pretend to be one
(no comments, no arbitrary nesting, no streaming).

### 2.2 What "the report regenerates from scratch" means for a document that already has hand-written narrative

`docs/validation-report.md` is not currently a data dump — Phases 1–5 added
real prose alongside the numbers: the determinism-math resolution, three
CI-only bug writeups, the false-sharing null result, the LSM inception-
exercise bug, the frozen-vs-naive sweep discussion. A script cannot
regenerate "we found a deadlock caused by a pre-release Clang's
`condition_variable_any` integration" — that narrative only exists because
someone (me, working through it) wrote it down.

CLAUDE.md's Phase 6 gate criterion is "report regenerates from scratch via
one command," and separately (§6 Phase 6) asks for "a report generator
producing `docs/validation-report.md` with all convergence plots, the VR
table, the scaling curve, the bump-size study, and the CFA invariant results
table" — i.e., specifically the *data-driven* artifacts, not the narrative
prose around them (those five items are exactly the numeric/plot content
already in the document, not the bug writeups).

Proposed resolution: the generator regenerates a clearly-delimited section
(`<!-- BEGIN GENERATED --> ... <!-- END GENERATED -->`) inside
`docs/validation-report.md` containing exactly those five items, computed
live from the engine via the Python bindings each run. Everything outside
that block (all the phase-by-phase narrative) is untouched by the generator
and continues to be hand-maintained prose, exactly as it's been for five
phases. This satisfies the literal gate ("regenerates from scratch via one
command" — the generated block does, in full) without deleting real
findings. Flagging this rather than deciding it silently, since it changes
how a document five phases of work already went into gets maintained going
forward.

## 3. `mcd_cli`

### 3.1 Protocol: one JSON object on stdin, one JSON object on stdout

```
$ echo '{"product":"european","spot":100,"strike":100,"rate":0.05,
         "carry_yield":0.0,"vol":0.2,"time":1.0,"type":"call",
         "path_count":1000000,"seed":42}' | mcd_cli
{
  "price": 10.4506,
  "standard_error": 0.0163,
  "ci_95_low": 10.4187,
  "ci_95_high": 10.4825,
  "path_count": 1000000,
  "seed": 42,
  "elapsed_seconds": 0.0621,
  "paths_per_second": 16103061.2
}
```

Every Monte Carlo product's response carries all seven fields above — the
response struct's constructor requires them, so it is not possible in the
C++ type system to serialize a price without its CI (CLAUDE.md: "a price
reported without a confidence interval is an incomplete result"). Analytic
and binomial products (deterministic, no sampling error) return
`{"price": ..., "elapsed_seconds": ...}` only — `standard_error`/CI fields
are omitted rather than fabricated as zero, since zero would misrepresent
"no estimation error" as "estimation error measured and found to be zero."

`"product"` selects one of: `european` (analytic + MC), `digital`, `asian`,
`barrier`, `lookback`, `american` (LSM), `binomial_european`,
`binomial_american`, `forward`. Malformed input (bad JSON, unknown product,
missing required field) returns `{"error": "<message>"}` on stdout and a
non-zero exit code — never a crash, never partial/ambiguous output.

`elapsed_seconds` and `paths_per_second` come from a small new shared
primitive, `include/mcd/core/timing.hpp`
(`time_call(F&& f) -> TimingResult{result, elapsed_seconds}`), wrapping
`std::chrono::steady_clock` around the pricing call — used identically by
the CLI and exposed to Python (sec.4), so "the benchmark harness" CLAUDE.md
asks pybind11 to expose and the timing fields CLAUDE.md asks the CLI to
report are the same code, not two implementations of the same idea.

### 3.2 Greeks and American requests

`{"product":"european","request":"greeks", ...}` returns
`{"delta":...,"gamma":...,"vega":...,"theta":...,"rho":...}` via
`finite_difference_european`. `{"product":"american", ...}` runs
`monte_carlo_lsm_american` and returns the same seven-field shape (the LSM
policy itself is not serialized — it's an internal reuse mechanism for
Greeks, not a user-facing result).

## 4. Python bindings (pybind11, `mcd` extension module)

Built independently of the CMake project (which exists for the C++ test/
benchmark suite and its GoogleTest/Google Benchmark FetchContent
dependencies, neither of which the Python extension needs): a root-level
`setup.py` lists the engine's own `.cpp` sources directly and uses pybind11's
bundled `setup_helpers.Pybind11Extension` (ships inside the `pybind11` PyPI
package CLAUDE.md already approved as a build-time requirement — no
additional package needed for `pip install -e .` to work, and no CMake
FetchContent involved on this path at all).

Exposed surface: every analytic pricer (§1 Phase 1), `crr_binomial` /
`crr_binomial_american`, every `monte_carlo_*` pricer plus `McOptions`,
`monte_carlo_lsm_american`, `finite_difference_european` /
`default_bump_sizes`, and `time_call`-based timing so Python callers get the
same paths/second measurement the CLI reports. `OptionType` and the other
enums (§1 core/types.hpp) become Python enums.

**GIL released around every pricing call** (`py::call_guard<
py::gil_scoped_release>()`), per CLAUDE.md's explicit requirement — verified
by a test that runs two pricing calls concurrently from Python threads and
confirms wall time is close to the single-call time, not 2×, which would
only be possible if the GIL were actually released during the C++ work.

## 5. Report generator

`tools/generate_report.py`, using the Python bindings (dogfooding them
rather than duplicating pricer logic in a second language) to:

1. Recompute the CFA invariant checks (Phase 1) live and render a
   pass/fail + measured-value table.
2. Re-run the convergence sweep (Phase 3's log-log SE-vs-path-count study)
   and regenerate the SVG.
3. Re-run the variance-reduction factor measurements (Phase 3) and
   regenerate the table.
4. Re-run the bump-size sweep (Phase 5) and regenerate the SVG.
5. Re-run the thread-scaling sweep (Phase 4) and regenerate the SVG —
   parallel efficiency and Amdahl fit recomputed on whatever hardware the
   generator runs on, not copy-pasted from this machine's numbers.

SVG generation reuses the same hand-written-SVG approach already used
(without a plotting library) in Phases 3–5, now as a committed,
reproducible script instead of a throwaway one-off. Writes into the
generated block described in sec.2.2. One command: `python
tools/generate_report.py`.

## 6. README rewrite

Per CLAUDE.md §8: opens with one sentence on what this is, a results table
(peak paths/second with hardware, parallel efficiency, max validation error
in units of SE, product count, test count — all pulled from already-measured
numbers in `docs/validation-report.md`, not re-derived), the Phase 4 scaling
chart, and build/run in three commands — all in the first screenful. Then
architecture overview, design-decision rationale (Philox/determinism, thread
pool over OpenMP, streaming paths, the `-ffast-math` rejection, statistical
tolerances), validation methodology, CFA mapping, and the documented
limitations from Phase 5 §3.6.

## 7. Test plan

- Hand-written JSON encode/decode: round-trip tests, number formatting,
  string escaping — tested the same way every other hand-written primitive
  in this project is (own test file, real assertions, no fixed-epsilon
  Monte Carlo comparisons since this code is deterministic).
- `mcd_cli` integration tests: invoke the built binary as a subprocess for
  every product family; assert valid JSON, all required fields present, and
  price within 3 SE of the Phase 1 analytic oracle where one exists.
  Malformed input → clean JSON error + non-zero exit, asserted explicitly.
- Python smoke test (`bindings/python/tests/test_smoke.py`): import the
  built module, price every product family, assert CI fields present and
  values agree with the C++ test suite's own oracles.
- GIL-release test: concurrent Python-threaded pricing calls complete in
  close to single-call wall time.
- Report generator: run it, assert exit 0, assert the generated CFA
  invariant table is all-green (cross-checking the live Python-bindings
  computation against the hard C++ test suite's own invariant tests, which
  must independently agree).

## 8. Acceptance criteria

1. All of sec.7 passing.
2. `pip install -e .` works from a clean checkout.
3. Python smoke test prices every product.
4. `python tools/generate_report.py` regenerates the generated block of
   `docs/validation-report.md` from scratch, and the CLI's own output
   schema makes an incomplete (CI-less) Monte Carlo result unrepresentable.
5. No forbidden compiler flags; no new third-party dependency beyond
   pybind11 (already approved) and the hand-written JSON reader/writer
   (sec.2.1, flagged for approval here rather than silently added).

## 9. Open questions for you

1. **JSON approach** (sec.2.1): hand-write a minimal, scope-limited
   reader/writer (matches this project's existing pattern), or add a header-
   only JSON library as a named exception to §5?
2. **Report-generator scope** (sec.2.2): regenerate a clearly-delimited
   block inside the existing `docs/validation-report.md` (preserving five
   phases of hand-written narrative around it), or something else?
