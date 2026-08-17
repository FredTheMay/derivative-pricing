#pragma once

#include "mcd/core/types.hpp"

namespace mcd::pricers {

struct BinomialResult {
    double price;
    double risk_neutral_probability;
    double up_factor;
    double down_factor;
};

// Cox-Ross-Rubinstein binomial tree, European exercise, continuous carry yield q.
// `steps == 1` gives the one-period tree; the risk-neutral probability and up/down
// factors are exposed explicitly so callers (and tests) can inspect them directly.
[[nodiscard]] BinomialResult crr_binomial(double spot, double strike, double rate,
                                           double carry_yield, double vol, double time, int steps,
                                           OptionType type) noexcept;

// Cox-Ross-Rubinstein binomial tree, American exercise: at every node, the backward-induction
// value is max(continuation, immediate exercise). Independent oracle for Phase 5's
// Longstaff-Schwartz American pricer -- a fine (large `steps`) American binomial tree converges
// to the true American price from a completely different numerical method (tree vs. regression
// Monte Carlo), which is exactly the kind of independent reference CLAUDE.md sec.2 requires.
[[nodiscard]] double crr_binomial_american(double spot, double strike, double rate,
                                            double carry_yield, double vol, double time, int steps,
                                            OptionType type) noexcept;

} // namespace mcd::pricers
