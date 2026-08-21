#pragma once

#include "mcd/core/types.hpp"

#include <cstdint>

namespace mcd::greeks {

// Pathwise-derivative Greeks: differentiates the *payoff along the simulated path*,
// holding the random draws fixed, rather than the density (likelihood-ratio) or the
// price via bumped re-pricing (finite differences). See
// docs/design/09-pathwise-greeks.md for the derivation and the FD/pathwise/LR
// comparison this stretch goal exists to produce.
struct PathwiseGreeksResult {
    double value = 0.0;
    double standard_error = 0.0;
};

// No gamma field: not "not computed for this product" (that's LR's barrier::theta
// situation) but genuinely undefined for pathwise on every product -- gamma would
// require differentiating an indicator function (a Dirac delta, not a number). See
// docs/design/09-pathwise-greeks.md sec.3.
struct PathwiseGreeks {
    PathwiseGreeksResult delta, vega, rho;
};

[[nodiscard]] PathwiseGreeks pathwise_european(double spot, double strike, double rate,
                                                double carry_yield, double vol, double time,
                                                OptionType type, std::uint64_t path_count,
                                                std::uint64_t seed) noexcept;

[[nodiscard]] PathwiseGreeks pathwise_asian(double spot, double strike, double rate,
                                             double carry_yield, double vol, double time,
                                             OptionType type, StrikeStyle strike_style,
                                             AverageStyle average_style, int monitoring_points,
                                             std::uint64_t path_count,
                                             std::uint64_t seed) noexcept;

// Deliberately demonstrates pathwise's failure mode rather than refusing to compile it:
// a cash-or-nothing digital's payoff is piecewise *constant*, so its pathwise slope is
// zero everywhere except exactly at the strike (probability zero of being sampled) --
// this estimator converges to exactly 0.0, which is not the digital's true delta. Used
// by tests/pathwise_test.cpp to measure and document the failure with real numbers, not
// just assert it in prose. Not exposed through mcd_cli/bindings/the AWS demo -- a
// documented failure mode, not a product feature.
[[nodiscard]] PathwiseGreeksResult pathwise_digital_delta_naive_and_broken(
    double spot, double strike, double rate, double carry_yield, double vol, double time,
    OptionType type, double cash_amount, std::uint64_t path_count,
    std::uint64_t seed) noexcept;

} // namespace mcd::greeks
