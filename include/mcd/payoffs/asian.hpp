#pragma once

#include "mcd/core/types.hpp"
#include "mcd/payoffs/path_payoff.hpp"

#include <algorithm>
#include <cmath>

namespace mcd::payoffs {

// Arithmetic-average Asian, fixed or floating strike. Averages over the
// monitored dates only (not t=0); see docs/design/03-exotics-variance-
// reduction.md sec.2 for the convention.
struct ArithmeticAsianPayoff {
    double strike = 0.0;
    OptionType type = OptionType::Call;
    StrikeStyle style = StrikeStyle::Fixed;

    double sum = 0.0;
    double last_price = 0.0;
    unsigned count = 0;

    void observe(double price) noexcept {
        sum += price;
        last_price = price;
        ++count;
    }

    [[nodiscard]] double result() const noexcept {
        const double average = sum / static_cast<double>(count);
        const double phi = type == OptionType::Call ? 1.0 : -1.0;
        const double reference = style == StrikeStyle::Fixed ? (average - strike)
                                                               : (last_price - average);
        return std::max(phi * reference, 0.0);
    }
};

static_assert(PathPayoff<ArithmeticAsianPayoff>);

// Geometric-average Asian, fixed or floating strike. Also serves as the
// control variate for arithmetic Asian (its analytic price is Phase 1's
// kemna_vorst).
struct GeometricAsianPayoff {
    double strike = 0.0;
    OptionType type = OptionType::Call;
    StrikeStyle style = StrikeStyle::Fixed;

    double log_sum = 0.0;
    double last_price = 0.0;
    unsigned count = 0;

    void observe(double price) noexcept {
        log_sum += std::log(price);
        last_price = price;
        ++count;
    }

    [[nodiscard]] double result() const noexcept {
        const double average = std::exp(log_sum / static_cast<double>(count));
        const double phi = type == OptionType::Call ? 1.0 : -1.0;
        const double reference = style == StrikeStyle::Fixed ? (average - strike)
                                                               : (last_price - average);
        return std::max(phi * reference, 0.0);
    }
};

static_assert(PathPayoff<GeometricAsianPayoff>);

} // namespace mcd::payoffs
