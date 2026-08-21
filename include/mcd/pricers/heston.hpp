#pragma once

#include "mcd/core/types.hpp"
#include "mcd/models/heston.hpp"

#include <cstdint>

namespace mcd::pricers {

struct HestonMcResult {
    double price = 0.0;
    double standard_error = 0.0;
    std::uint64_t path_count = 0;
};

// Andersen's (2008) Quadratic-Exponential scheme (docs/design/12-heston.md sec.3):
// simulates the CIR variance process without ever going negative, even when the
// Feller condition (2*kappa*theta > xi^2) fails, then updates log-spot via the
// martingale-corrected K0-K4 discretization (central weighting, gamma1=gamma2=0.5).
// monitoring_points is the number of discretization steps across (0, T] -- unlike
// GBM's exact one-shot simulation, Heston has no closed-form terminal distribution,
// so this genuinely needs multiple steps even for a European payoff.
[[nodiscard]] HestonMcResult heston_qe_european(const models::HestonParams& params,
                                                 double strike, OptionType type,
                                                 int monitoring_points, std::uint64_t path_count,
                                                 std::uint64_t seed) noexcept;

// Heston's (1993) semi-analytic characteristic-function price, using the
// branch-cut-safe "Little Trap" form (Albrecher, Mayer, Schoutens & Tistaert 2007).
// The independent oracle heston_qe_european is validated against (docs/design/12-
// heston.md sec.5). Deterministic -- no seed, no standard error.
[[nodiscard]] double heston_semi_analytic(const models::HestonParams& params, double strike,
                                           OptionType type) noexcept;

} // namespace mcd::pricers
