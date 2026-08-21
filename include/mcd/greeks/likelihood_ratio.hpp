#pragma once

#include "mcd/core/types.hpp"

#include <cstdint>
#include <optional>

namespace mcd::greeks {

// Likelihood-ratio (score-function) Greeks: differentiates the path's probability density
// with respect to each parameter, not the payoff -- so it needs no smoothness from the
// payoff at all. Fixes finite-difference Greeks' documented weak point (gamma for
// discontinuous payoffs: digitals, barriers near the boundary). See
// docs/design/08-likelihood-ratio-greeks.md for the full derivation.
struct LrGreeksResult {
    double value = 0.0;
    double standard_error = 0.0;
};

struct LrGreeks {
    LrGreeksResult delta, gamma, vega, rho;
    // European/digital always populate this. Barrier deliberately does not: unlike
    // delta/gamma/vega/rho, discretely-monitored path-dependent theta entangles the
    // monitoring step size (dt = T/n) with time-to-expiry itself in a way that doesn't
    // reduce to the clean per-step-sum generalization the other four Greeks have --
    // nullopt here means "not computed," never a silent, misleading zero. See
    // docs/design/08-likelihood-ratio-greeks.md sec.4.
    std::optional<LrGreeksResult> theta;
};

[[nodiscard]] LrGreeks likelihood_ratio_european(double spot, double strike, double rate,
                                                   double carry_yield, double vol, double time,
                                                   OptionType type, std::uint64_t path_count,
                                                   std::uint64_t seed) noexcept;

[[nodiscard]] LrGreeks likelihood_ratio_digital(double spot, double strike, double rate,
                                                  double carry_yield, double vol, double time,
                                                  OptionType type, DigitalStyle style,
                                                  double cash_amount, std::uint64_t path_count,
                                                  std::uint64_t seed) noexcept;

[[nodiscard]] LrGreeks likelihood_ratio_barrier(double spot, double strike, double barrier,
                                                  double rate, double carry_yield, double vol,
                                                  double time, OptionType type,
                                                  BarrierDirection direction, BarrierKnock knock,
                                                  double rebate, int monitoring_points,
                                                  std::uint64_t path_count,
                                                  std::uint64_t seed) noexcept;

} // namespace mcd::greeks
