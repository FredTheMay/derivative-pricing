#pragma once

#include "mcd/core/types.hpp"
#include "mcd/payoffs/path_payoff.hpp"

#include <algorithm>

namespace mcd::payoffs {

// Fixed-strike lookback: payoff compares the realized extremum to a fixed
// strike. Running extremum is seeded from the initial spot (matching Phase
// 1's analytic treatment, which prices at inception).
struct LookbackFixedPayoff {
    double strike;
    OptionType type;
    double running_max;
    double running_min;

    LookbackFixedPayoff(double initial_spot, double strike_, OptionType type_) noexcept
        : strike(strike_), type(type_), running_max(initial_spot), running_min(initial_spot) {}

    void observe(double price) noexcept {
        running_max = std::max(running_max, price);
        running_min = std::min(running_min, price);
    }

    [[nodiscard]] double result() const noexcept {
        return type == OptionType::Call ? std::max(running_max - strike, 0.0)
                                         : std::max(strike - running_min, 0.0);
    }
};

static_assert(PathPayoff<LookbackFixedPayoff>);

// Floating-strike lookback: payoff compares terminal spot to the realized
// extremum. Payoff is always non-negative by construction (running_min <=
// last_price <= running_max, pathwise).
struct LookbackFloatingPayoff {
    OptionType type;
    double last_price;
    double running_max;
    double running_min;

    LookbackFloatingPayoff(double initial_spot, OptionType type_) noexcept
        : type(type_), last_price(initial_spot), running_max(initial_spot),
          running_min(initial_spot) {}

    void observe(double price) noexcept {
        running_max = std::max(running_max, price);
        running_min = std::min(running_min, price);
        last_price = price;
    }

    [[nodiscard]] double result() const noexcept {
        return type == OptionType::Call ? (last_price - running_min) : (running_max - last_price);
    }
};

static_assert(PathPayoff<LookbackFloatingPayoff>);

} // namespace mcd::payoffs
