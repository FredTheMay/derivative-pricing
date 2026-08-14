#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <latch>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

namespace mcd {

// Generous fixed upper bound on logical_chunk_count() (== hardware_concurrency()), used
// to size a fixed-size stack array of per-chunk accumulators with zero heap allocation.
// No real machine approaches this today; it exists purely as a compile-time capacity.
inline constexpr unsigned kMaxLogicalChunks = 512;

namespace detail {

// Non-owning, zero-allocation type-erased callable reference (the standard
// "function_ref" pattern): stores a pointer to the referenced callable plus a
// trampoline function pointer. The referenced callable must outlive this
// TaskRef -- true here, since it's constructed on parallel_for's stack for
// the duration of its blocking call.
class TaskRef {
  public:
    template <typename F>
    explicit TaskRef(F& f) noexcept
        : ptr_(&f), invoke_([](void* p, unsigned chunk, std::uint64_t begin,
                                std::uint64_t end) { (*static_cast<F*>(p))(chunk, begin, end); }) {}

    void operator()(unsigned chunk, std::uint64_t begin, std::uint64_t end) const {
        invoke_(ptr_, chunk, begin, end);
    }

  private:
    void* ptr_;
    void (*invoke_)(void*, unsigned, std::uint64_t, std::uint64_t);
};

// Deterministic, static contiguous [begin, end) path-index range for a given
// chunk out of `chunk_count` chunks partitioning [0, total_paths). Depends
// only on (chunk_index, total_paths, chunk_count) -- never on which worker
// executes it or in what order chunks are claimed. See
// docs/design/04-parallelism.md sec.2.
[[nodiscard]] std::pair<std::uint64_t, std::uint64_t>
chunk_bounds(unsigned chunk_index, std::uint64_t total_paths, unsigned chunk_count) noexcept;

} // namespace detail

// Hand-written thread pool over std::jthread. Workers are persistent (created
// once, blocked on a condition variable between rounds). The number of
// logical chunks a path range is split into is FIXED at construction
// (hardware_concurrency()) and never varies with a given call's num_threads
// -- this is what makes bitwise-identical prices across thread counts
// possible: the sequence of floating-point operations that produces the
// final result depends only on the fixed chunk boundaries and a fixed
// chunk-index merge order, never on how many workers executed them. See
// docs/design/04-parallelism.md sec.2 for the full argument.
class ThreadPool {
  public:
    explicit ThreadPool(unsigned max_workers);
    ~ThreadPool();

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;
    ThreadPool(ThreadPool&&) = delete;
    ThreadPool& operator=(ThreadPool&&) = delete;

    [[nodiscard]] static unsigned logical_chunk_count() noexcept;

    // Runs fn(chunk_index, path_begin, path_end) once per logical chunk of
    // [0, total_paths). Only the first min(num_threads, max_workers) pool
    // workers participate in claiming chunks; every pool worker (active or
    // not) fully completes its round -- including re-entering the wait
    // state -- before this call returns, which is what makes it safe for
    // the caller to immediately start a new round without any stale-state
    // race against stragglers from this one. Blocks until complete.
    template <typename F>
    void parallel_for(std::uint64_t total_paths, unsigned num_threads, F&& fn) {
        detail::TaskRef task(fn);
        std::latch remaining(static_cast<std::ptrdiff_t>(workers_.size()));
        {
            std::lock_guard<std::mutex> lock(mutex_);
            task_ = &task;
            total_paths_ = total_paths;
            active_workers_ = num_threads < workers_.size() ? num_threads
                                                              : static_cast<unsigned>(workers_.size());
            remaining_ = &remaining;
            next_chunk_.store(0, std::memory_order_relaxed);
            ++generation_;
        }
        cv_.notify_all();
        remaining.wait();
    }

  private:
    void worker_loop(unsigned worker_index);

    std::vector<std::jthread> workers_;
    std::mutex mutex_;
    std::condition_variable cv_;

    // Manual shutdown flag, checked by the same predicate as new-round
    // detection below -- deliberately not using std::jthread's built-in
    // stop_token/condition_variable_any-wait integration for this: on one
    // Clang/libc++ toolchain encountered during development (an unusually
    // recent, likely near-trunk build; see docs/design/04-parallelism.md
    // sec.3), that specific stop-token-aware wait overload did not wake
    // threads on request_stop(), hanging shutdown even with zero rounds ever
    // run -- isolated with a minimal standalone repro before concluding it
    // wasn't a bug in this pool's round logic. A plain condition_variable
    // plus an explicit atomic flag is simpler, more portable, and doesn't
    // depend on that newer, less battle-tested library integration.
    // std::jthread is still very much in use, exactly per CLAUDE.md's
    // locked design -- only the stop-signaling mechanism is manual.
    std::atomic<bool> stop_requested_{false};

    // Round state, guarded by mutex_ for setup; workers read a consistent
    // snapshot while holding the lock, then release it before processing.
    std::uint64_t generation_ = 0;
    unsigned active_workers_ = 0;
    const detail::TaskRef* task_ = nullptr;
    std::uint64_t total_paths_ = 0;
    std::latch* remaining_ = nullptr;

    // Shared claim counter for chunk assignment -- safe to be lock-free
    // since which worker claims which chunk never affects the result.
    std::atomic<unsigned> next_chunk_{0};
};

// Lazily-constructed singleton sized to 2 * hardware_concurrency() workers,
// covering the full benchmark sweep range this project's Phase 4 tests use.
[[nodiscard]] ThreadPool& shared_thread_pool();

} // namespace mcd
