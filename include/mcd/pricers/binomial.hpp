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

} // namespace mcd::pricers
