# Phase 4 — Parallelism

Status: **implemented, Phase 4 gate passed** (see `docs/validation-report.md`
for full results, including a real thread-pool deadlock found and fixed
during implementation, and the < 80% efficiency analysis)

## 1. Purpose

Add a hand-written thread pool over `std::jthread`, static contiguous
chunking, padded per-thread Welford accumulators, and deterministic
reduction — then prove, empirically, that every product's price is bitwise
identical regardless of thread count. Per CLAUDE.md §6 Phase 4 and the
locked decisions in §4.

## 2. A real numerical tension in the locked design — needs your decision

CLAUDE.md states two things that are in tension with each other once you
work through the floating-point arithmetic:

- "Accumulators: One per thread... Reduction: Pairwise merge of per-thread
  Welford accumulators."
- "Prices must be bitwise identical for 1 thread and N threads, for every
  product, for every path count. This is a hard acceptance criterion, not an
  aspiration."

Here's the problem. Welford's merge formula (combining two already-aggregated
groups via their counts/means/M2s in closed form) is mathematically correct
but is **not** bit-identical to sequential accumulation, because it performs
a different sequence of floating-point operations. Concretely: summing
`s0..s3` as `((s0+s1)+s2)+s3` (sequential) is not guaranteed bit-identical to
splitting into `(s0+s1)` and `(s2+s3)` and adding those (chunked-then-merged)
— floating-point addition isn't associative. The same applies to Welford's
mean/M2 update. This is a well-known fact in reproducible-summation
literature, not something specific to this implementation.

The consequence: if "1 thread" means *one accumulator, built by sequentially
adding every path* (no merge involved at all), and "N threads" means *N
accumulators, each built sequentially over its own contiguous slice, then
combined via the merge formula*, these two computational paths perform
genuinely different floating-point operations — and there is no general
guarantee they land on the same bits. Increasing N (2 chunks vs. 4 chunks vs.
8 chunks) also changes the merge-tree shape, so even "N threads" vs. "M
threads" (both >1) aren't guaranteed to agree with each other under a naive
reading.

**The fix**: decouple the *chunk topology* (how many pieces the path range is
split into, and the fixed order they're merged in) from the *worker thread
count* (how many OS threads concurrently execute those chunks). If the number
of chunks and the merge order are **fixed** — independent of how many workers
are asked to process them — then the sequence of floating-point operations
that produces the final price never changes, regardless of `num_threads`.
Only wall-clock parallelism changes; the arithmetic doesn't.

Concretely: fix `logical_chunk_count = std::thread::hardware_concurrency()`
once, for the life of the program (not re-read per call). Every pricer call,
regardless of the requested `num_threads`, always partitions the path range
into exactly `logical_chunk_count` static contiguous chunks and always merges
them in a fixed left-to-right pass in chunk-index order. `num_threads`
controls only how many worker threads pull chunks to execute — with
`num_threads=1`, one worker executes all `logical_chunk_count` chunks in
sequence (in chunk-index order, so it's *also* equivalent, chunk-by-chunk, to
what any other thread count does); with `num_threads=hardware_concurrency()`,
each worker executes roughly one chunk. **Which worker executes which chunk
never affects the result** — only the fixed chunk boundaries and fixed
chunk-index merge order do — so distributing chunks to workers via a simple
shared atomic counter (rather than a rigid static round-robin) is safe and
doesn't compromise determinism; only the *chunk definition and merge order*
need to be static, not the worker-assignment mechanism.

This is a foundational, hard-to-reverse call — it changes what "1 thread"
even means computationally (always `hardware_concurrency()` logical chunks
processed by 1 worker, not "no chunking at all"). Flagging it exactly as
CLAUDE.md's own process asks: *"any change to... the threading model, the
determinism guarantee... requires approval,"* and this is a genuine fork the
document doesn't settle. I'd like your explicit sign-off before implementing
this, since I don't think the literal "one accumulator per thread" wording,
taken with a thread-count-varying chunk count, is achievable at true bit level
— and I'd rather confirm that reasoning with you than silently pick an
interpretation for a "hard acceptance criterion, not an aspiration."

## 3. Thread pool design — persistent (approved)

Worker threads are `std::jthread`s created once, at pool construction, and
live for the pool's lifetime, blocking on a `std::condition_variable_any`
(needed to compose with `std::jthread`'s `stop_token`) between rounds rather
than being spawned/joined per call. Design:

- **Singleton pool**: a function-local static in `monte_carlo.cpp`, lazily
  constructed on first use with `2 * hardware_concurrency()` workers (enough
  to cover the "up to 2×`hardware_concurrency()`" benchmark sweep). Every
  pricer call reuses it.
- **Generation counter**: `parallel_for` increments a shared generation
  counter and notifies all workers; each worker compares against the last
  generation it processed to detect new work, avoiding races around a simple
  boolean "has work" flag across repeated rounds.
- **Zero-allocation task dispatch**: persistent workers must accept a
  *different* callable type on every `parallel_for<F>` call, which normally
  pushes toward `std::function` (and its non-guaranteed small-object
  allocation). Instead, a minimal non-owning `TaskRef` (a `void*` plus a
  static trampoline function pointer, the standard "function_ref" pattern)
  type-erases the callable with zero allocation, valid because the caller's
  lambda outlives the blocking `parallel_for` call.
- **Per-round completion**: a `std::latch` sized to `logical_chunk_count()`,
  constructed on `parallel_for`'s own stack (its lifetime naturally spans the
  whole blocking call, so no heap allocation or dynamic lifetime management
  needed), referenced by workers via a shared pointer. Each worker
  `count_down()`s once per chunk it completes; `parallel_for` returns once
  `latch.wait()` unblocks.
- **Static contiguous chunk claiming**: chunk *boundaries* are precomputed
  and fixed (per §2); which worker executes which chunk is decided by a
  shared `std::atomic<unsigned>` claim counter, since (per §2's argument)
  worker-to-chunk assignment never affects the deterministic result — only
  the chunk boundaries and fixed merge order do.
- **Variable per-call worker count**: `parallel_for(total_paths, num_threads,
  fn)` — only workers with `index < num_threads` participate in a given
  round; the rest see they're not needed and go back to waiting. This lets
  one persistent pool, sized once for the maximum benchmarked thread count,
  serve every `num_threads` value the tests and benchmarks sweep.
- **Shutdown**: relies on `std::jthread`'s built-in `stop_token` — the pool's
  destructor requests stop (implicit via `jthread`'s destructor) and
  `condition_variable_any::wait(lock, stop_token, predicate)` wakes workers
  to observe it and exit cleanly.

## 4. Interfaces

```cpp
// include/mcd/core/thread_pool.hpp
namespace mcd {

class ThreadPool {
  public:
    explicit ThreadPool(unsigned max_workers);
    ~ThreadPool(); // requests stop, joins via jthread destructor semantics
    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    // Fixed once at construction, independent of any later parallel_for's
    // num_threads argument.
    [[nodiscard]] static unsigned logical_chunk_count() noexcept; // hardware_concurrency()

    // Runs fn(chunk_index, path_begin, path_end) once per logical chunk of
    // [0, total_paths), with only the first `num_threads` (<= max_workers)
    // pool workers participating. Blocks until every chunk has run.
    template <typename F>
    void parallel_for(std::uint64_t total_paths, unsigned num_threads, F&& fn);
};

// Lazily-constructed singleton, sized to 2 * hardware_concurrency() workers.
[[nodiscard]] ThreadPool& shared_thread_pool();

} // namespace mcd
```

```cpp
// include/mcd/core/stats.hpp -- addition
namespace mcd {
class WelfordAccumulator {
  public:
    // ... existing members unchanged ...
    // Deterministic pairwise merge (Chan et al.): folds `other` into *this*.
    // Merging with an empty accumulator (count==0) is an exact identity.
    void merge(const WelfordAccumulator& other) noexcept;
};

// Padded to avoid false sharing between accumulators in a per-chunk array;
// alignas both starts each instance on its own cache line AND pads sizeof()
// to a multiple of the alignment, so array elements never share a line.
struct alignas(std::hardware_destructive_interference_size) PaddedWelford {
    WelfordAccumulator acc;
};
} // namespace mcd
```

```cpp
// include/mcd/pricers/monte_carlo.hpp -- McOptions addition
struct McOptions {
    bool antithetic = false;
    bool control_variate = false;
    bool brownian_bridge = false;
    unsigned num_threads = 1; // 1 = single-threaded, unchanged from Phase 2/3 behavior
};
```

No pricer signatures change shape — `num_threads` rides in the existing
`McOptions` struct (already a default-valued trailing parameter on every
Phase 3 pricer, and on the `monte_carlo_european` overload that has it). The
zero-argument `monte_carlo_european` overload keeps its exact Phase 2
signature and stays single-threaded, unchanged.

Every pricer's internal loop changes from a single `WelfordAccumulator` to:
build `logical_chunk_count` chunk results (each a `PaddedWelford`, computed
via the existing per-path logic, run either directly in-loop when
`num_threads==1` or via `ThreadPool::parallel_for` otherwise), then fold them
into a final `WelfordAccumulator` via `merge()` in fixed chunk-index order.

## 5. Benchmarks and required artifacts

- Paths/second and ns/path vs. thread count, 1 → 2×`hardware_concurrency()`,
  European pricer (reuses Phase 2's `bench/mc_bench.cpp` harness, extended
  with a thread-count axis).
- Speedup and parallel efficiency curve; fit and report Amdahl's serial
  fraction from the measured curve.
- False-sharing A/B: a deliberately unpadded accumulator array benchmarked
  against `PaddedWelford`, reporting the measured difference — not claimed
  without a number.
- Every benchmark record: CPU model, core count, compiler + version, flags,
  turbo/boost state (best-effort reported, since this dev machine doesn't
  expose a clean OS toggle), median of ≥5 runs. Same honesty bar as Phase 2's
  benchmark record — this is a laptop dev machine, not a controlled bench
  box, and I'll say so again rather than overstate rigor.

## 6. Test plan

- **Bitwise determinism**: for every product, thread counts
  `{1, 2, 4, 8, hardware_concurrency()}`, `std::bit_cast<uint64_t>` equality
  (integer comparison, not `EXPECT_DOUBLE_EQ`) — per CLAUDE.md's explicit
  instruction on how to test this.
- **Pool correctness**: no lost work (sum of per-chunk path counts equals
  total), no double execution, correct behavior when `path_count <
  logical_chunk_count` (some chunks legitimately empty — `merge()` with an
  empty accumulator must be a true identity, tested directly).
- **TSan clean** under the full suite.
- **ASan, UBSan clean** (already true through Phase 3; re-verified here now
  that real concurrency exists).
- `WelfordAccumulator::merge()` unit tests: matches a direct two-pass
  computation over the combined data, and empty-accumulator identity, to
  1e-12.

Known local-machine caveat (documented already in
`docs/design/00-requirements.md` §6a): ASan and TSan binaries hang/crash at
startup on this specific Mac (Apple clang 17, macOS 26.5.2) for reasons
unrelated to this project's code. Phase 4's TSan requirement will be verified
via CI (Linux, GCC/Clang from apt) rather than locally, exactly as Phase 0
anticipated when it put sanitizers in the CI matrix instead of relying on
local runs.

## 7. Acceptance criteria

1. Bitwise determinism test green for every product, every tested thread
   count.
2. TSan clean (CI).
3. ASan, UBSan clean.
4. Parallel efficiency ≥ 80% at physical core count, or a written analysis
   of why not, per CLAUDE.md's explicit fallback clause.
5. All benchmark artifacts in §5 produced from real measurements this
   session.
6. No forbidden compiler flags; no OpenMP/`std::async`/
   `std::execution::par_unseq`.

## 8. Open questions for you

1. **§2, the big one**: confirm the fixed-logical-chunk-count design
   (`logical_chunk_count = hardware_concurrency()`, invariant across
   `num_threads`) as the resolution to the bitwise-determinism requirement.
   This is the one I most need your explicit sign-off on before writing any
   code.
2. **§3**: spawn-per-call thread pool (my recommendation) vs. a persistent
   pool from the start.
