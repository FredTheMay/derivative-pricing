#pragma once

#include "mcd/core/types.hpp"

#include <cstdint>

namespace mcd::greeks {

struct EuropeanGreeks {
    double delta;
    double gamma;
    double vega;
    double theta;
    double rho;
};

struct BumpSizes {
    double spot; // delta, gamma
    double vol;  // vega
    double rate; // rho
    double time; // theta
};

// Defaults are the outcome of the bump-size study in docs/design/05-greeks-and-american.md
// sec.2.3 / docs/validation-report.md, not a guess: gamma (the binding constraint, per
// CLAUDE.md sec.6 Phase 5) sets the relative spot bump, and the same relative scale is
// reused for vol/rate/time since the underlying trade-off (truncation error vs. MC noise
// amplified by dividing by a shrinking h) is the same shape for each.
[[nodiscard]] BumpSizes default_bump_sizes(double spot, double vol, double time) noexcept;

// Central-difference Greeks for a European option, computed by repeated calls to
// monte_carlo_european at the identical (seed, path_count) for every bumped scenario.
// Common random numbers falls out automatically: Phase 2's RNG counter is keyed on
// (seed, path_index, draw_index) only, never on the priced parameters, so bumping any
// parameter and re-pricing with the same (seed, path_count) reuses the identical draw for
// every path. See docs/design/05-greeks-and-american.md sec.2.1.
[[nodiscard]] EuropeanGreeks finite_difference_european(double spot, double strike, double rate,
                                                          double carry_yield, double vol,
                                                          double time, OptionType type,
                                                          std::uint64_t path_count,
                                                          std::uint64_t seed,
                                                          BumpSizes bumps) noexcept;

} // namespace mcd::greeks
