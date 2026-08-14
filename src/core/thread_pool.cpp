#include "mcd/core/thread_pool.hpp"

#include <algorithm>

namespace mcd {

namespace detail {

std::pair<std::uint64_t, std::uint64_t> chunk_bounds(unsigned chunk_index,
                                                       std::uint64_t total_paths,
                                                       unsigned chunk_count) noexcept {
    const std::uint64_t base = total_paths / chunk_count;
    const std::uint64_t remainder = total_paths % chunk_count;
    const std::uint64_t begin =
        chunk_index * base + std::min<std::uint64_t>(chunk_index, remainder);
    const std::uint64_t end = begin + base + (chunk_index < remainder ? 1 : 0);
    return {begin, end};
}

} // namespace detail

ThreadPool::ThreadPool(unsigned max_workers) {
    workers_.reserve(max_workers);
    for (unsigned i = 0; i < max_workers; ++i) {
        workers_.emplace_back([this, i] { worker_loop(i); });
    }
}

ThreadPool::~ThreadPool() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stop_requested_.store(true, std::memory_order_relaxed);
    }
    cv_.notify_all();
    // workers_ (std::vector<std::jthread>) destructs next, joining each thread; every
    // worker will have woken via the notify above, observed stop_requested_, and returned
    // from worker_loop -- see the member comment in thread_pool.hpp for why this is manual
    // rather than using jthread's built-in stop_token/condition_variable_any integration.
}

unsigned ThreadPool::logical_chunk_count() noexcept {
    const unsigned hc = std::thread::hardware_concurrency();
    return hc > 0 ? hc : 1;
}

void ThreadPool::worker_loop(unsigned worker_index) {
    std::uint64_t last_generation = 0;
    while (true) {
        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait(lock, [&] {
            return generation_ != last_generation || stop_requested_.load(std::memory_order_relaxed);
        });
        if (stop_requested_.load(std::memory_order_relaxed)) {
            return;
        }

        last_generation = generation_;
        const unsigned active = active_workers_;
        const detail::TaskRef* task = task_;
        const std::uint64_t total_paths = total_paths_;
        std::latch* remaining = remaining_;
        lock.unlock();

        if (worker_index < active) {
            const unsigned chunk_count = logical_chunk_count();
            unsigned chunk = 0;
            while ((chunk = next_chunk_.fetch_add(1, std::memory_order_relaxed)) < chunk_count) {
                const auto [begin, end] = detail::chunk_bounds(chunk, total_paths, chunk_count);
                (*task)(chunk, begin, end);
            }
        }
        // Every pool worker -- active or not -- signals here, exactly once per round, only
        // after it has fully finished any chunk work. This is what guarantees parallel_for
        // cannot return (and the caller cannot start a new round) until every worker has
        // provably stopped touching this round's shared state, closing the straggler race
        // described in docs/design/04-parallelism.md sec.3.
        remaining->count_down();
    }
}

ThreadPool& shared_thread_pool() {
    static ThreadPool pool(2 * std::thread::hardware_concurrency());
    return pool;
}

} // namespace mcd
