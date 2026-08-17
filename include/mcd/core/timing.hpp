#pragma once

#include <chrono>
#include <utility>

namespace mcd {

template <typename Result>
struct TimingResult {
    Result value;
    double elapsed_seconds;
};

// Wraps a callable with a wall-clock timer. Shared by mcd_cli (which reports
// elapsed_seconds/paths_per_second on every result) and the Python bindings'
// benchmark harness (CLAUDE.md sec.6 Phase 6) -- one implementation of "how
// long did this pricing call take," not two. See
// docs/design/06-cli-bindings-reporting.md sec.3.1.
template <typename F>
[[nodiscard]] auto time_call(F&& f) {
    const auto start = std::chrono::steady_clock::now();
    auto result = std::forward<F>(f)();
    const auto end = std::chrono::steady_clock::now();
    const double elapsed = std::chrono::duration<double>(end - start).count();
    return TimingResult<decltype(result)>{.value = std::move(result), .elapsed_seconds = elapsed};
}

} // namespace mcd
