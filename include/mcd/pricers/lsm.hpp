#pragma once

#include "mcd/core/types.hpp"

#include <cstdint>
#include <vector>

namespace mcd::pricers {

// Fitted regression coefficients per exercise date (index k = step k*dt, for
// k = 1 .. monitoring_points-1; index 0 and monitoring_points are always empty --
// no exercise decision is made at inception or at maturity, where exercise and payoff
// coincide). An empty vector at a populated index means too few in-the-money paths were
// available to regress at that date, matching a base run where no exercise decisions
// were made there either. See docs/design/05-greeks-and-american.md sec.3.5.
struct LsmPolicy {
    std::vector<std::vector<double>> coefficients_by_date;
    // Whether the base run's inception-time decision (compare immediate exercise value at
    // t=0, a deterministic scalar comparison with no path regression involved, against the
    // regression-estimated continuation value) was to exercise immediately. Frozen and
    // replayed as-is by reprice_against_frozen_policy for the same reason every other date's
    // decision is frozen: re-deciding under a bumped scenario can flip this discontinuously.
    bool exercise_at_inception = false;
};

struct LsmResult {
    double price = 0.0;
    double standard_error = 0.0;
    LsmPolicy policy; // always populated; reusable for frozen-boundary Greeks
};

// Longstaff-Schwartz American option pricing. Single-threaded (see
// docs/design/05-greeks-and-american.md sec.7 open question 2); stores every path's price
// at every monitoring date (CLAUDE.md's one explicit, named exception to the
// streaming/zero-allocation rule -- backward induction needs every path simultaneously to
// regress continuation value). Basis: Laguerre polynomials degree 3 on x = S/K. Regression
// solved via Householder QR (mcd::householder_least_squares), not the normal equations.
//
// LSM is a documented lower-bound estimator: the fitted continuation value is an
// approximation, and using it to make exercise decisions can only leave value on the
// table relative to the true optimal policy, never exceed it.
[[nodiscard]] LsmResult monte_carlo_lsm_american(double spot, double strike, double rate,
                                                   double carry_yield, double vol, double time,
                                                   OptionType type, int monitoring_points,
                                                   std::uint64_t path_count,
                                                   std::uint64_t seed) noexcept;

// Re-prices under (possibly bumped) parameters using a *frozen* policy from a base run
// instead of refitting the regression at every exercise date. This is what makes American
// Greeks usable at all: bumping spot and refitting moves the exercise boundary
// discontinuously, producing an extremely noisy delta (CLAUDE.md sec.6 Phase 5). Requires
// the same monitoring_points and path_count as the run that produced `policy`, so the same
// (seed, path_index, step) draws are consumed in the same order -- common random numbers.
[[nodiscard]] double reprice_against_frozen_policy(double spot, double strike, double rate,
                                                     double carry_yield, double vol, double time,
                                                     OptionType type, int monitoring_points,
                                                     std::uint64_t path_count, std::uint64_t seed,
                                                     const LsmPolicy& policy) noexcept;

} // namespace mcd::pricers
