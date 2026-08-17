#include "mcd/greeks/finite_difference.hpp"

#include "mcd/pricers/monte_carlo.hpp"

#include <algorithm>

namespace mcd::greeks {

// Spot fraction is the measured outcome of the real sweep in
// docs/validation-report.md Phase 5 (gamma error vs. h/S, N=200,000 paths, seed=777,
// S=K=100, r=0.05, q=0, sigma=0.20, T=1, European call): gamma error is catastrophic
// below h/S ~ 1e-4 (MC-noise regime, error ~ SE/h^2), falls to a broad low-error plateau
// over roughly h/S in [3e-3, 1e-1] (measured minimum 2.05e-6 at h/S = 3.16e-3), then rises
// again past h/S ~ 3e-1 (truncation regime, error ~ h^2) -- see
// docs/benchmarks/phase5-bump-size-sweep.svg. Delta's error is flat (~6e-4) across the
// entire MC-noise-dominated range and only degrades once h/S exceeds ~3e-2, so gamma is
// the binding constraint, exactly as CLAUDE.md sec.6 Phase 5 anticipates. 1% is chosen
// over the single-seed measured minimum (0.316%) as a round, seed-independent default
// still comfortably inside the measured low-error plateau. Vol and time reuse the same
// relative scale since both face the same truncation-vs-MC-noise trade-off shape (not
// independently swept -- only spot was, per the design doc's sec.2.3 scope). Rate uses a
// fixed absolute bump (100bp) since relative bumps are meaningless near r=0.
namespace {
constexpr double kSpotBumpFraction = 0.01;
constexpr double kVolBumpFraction = 0.01;
constexpr double kTimeBumpFraction = 0.01;
constexpr double kRateAbsoluteBump = 0.01;
constexpr double kMinAbsoluteBump = 1e-4;
} // namespace

BumpSizes default_bump_sizes(double spot, double vol, double time) noexcept {
    return BumpSizes{
        .spot = std::max(spot * kSpotBumpFraction, kMinAbsoluteBump),
        .vol = std::max(vol * kVolBumpFraction, kMinAbsoluteBump),
        .rate = kRateAbsoluteBump,
        .time = std::max(time * kTimeBumpFraction, kMinAbsoluteBump),
    };
}

EuropeanGreeks finite_difference_european(double spot, double strike, double rate,
                                           double carry_yield, double vol, double time,
                                           OptionType type, std::uint64_t path_count,
                                           std::uint64_t seed, BumpSizes bumps) noexcept {
    const auto price = [&](double s, double r, double q, double v, double t) {
        return pricers::monte_carlo_european(s, strike, r, q, v, t, type, path_count, seed).price;
    };

    const double v0 = price(spot, rate, carry_yield, vol, time);
    const double v_spot_up = price(spot + bumps.spot, rate, carry_yield, vol, time);
    const double v_spot_down = price(spot - bumps.spot, rate, carry_yield, vol, time);
    const double v_vol_up = price(spot, rate, carry_yield, vol + bumps.vol, time);
    const double v_vol_down = price(spot, rate, carry_yield, vol - bumps.vol, time);
    const double v_rate_up = price(spot, rate + bumps.rate, carry_yield, vol, time);
    const double v_rate_down = price(spot, rate - bumps.rate, carry_yield, vol, time);
    const double v_time_up = price(spot, rate, carry_yield, vol, time + bumps.time);
    const double v_time_down = price(spot, rate, carry_yield, vol, time - bumps.time);

    return EuropeanGreeks{
        .delta = (v_spot_up - v_spot_down) / (2.0 * bumps.spot),
        .gamma = (v_spot_up - 2.0 * v0 + v_spot_down) / (bumps.spot * bumps.spot),
        .vega = (v_vol_up - v_vol_down) / (2.0 * bumps.vol),
        .theta = -(v_time_up - v_time_down) / (2.0 * bumps.time),
        .rho = (v_rate_up - v_rate_down) / (2.0 * bumps.rate),
    };
}

} // namespace mcd::greeks
