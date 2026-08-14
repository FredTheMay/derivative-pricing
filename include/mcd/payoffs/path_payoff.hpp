#pragma once

#include <concepts>

namespace mcd::payoffs {

// A path-dependent payoff: observes each monitored price as the path unfolds
// and produces its (undiscounted) result once the path is complete. Unlike
// Payoff (european.hpp), which sees only the terminal spot, these hold
// running state -- but only ever O(1) scalars (a sum, a count, a min/max, a
// breached flag), never the path itself.
template <typename P>
concept PathPayoff = requires(P& p, double price) {
    { p.observe(price) } -> std::same_as<void>;
    { p.result() } -> std::convertible_to<double>;
};

} // namespace mcd::payoffs
