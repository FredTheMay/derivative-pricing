#pragma once

#include "mcd/core/types.hpp"

#include <algorithm>
#include <concepts>

namespace mcd::payoffs {

template <typename P>
concept Payoff = requires(const P& p, double terminal_spot) {
    { p(terminal_spot) } -> std::convertible_to<double>;
};

struct EuropeanPayoff {
    double strike = 0.0;
    OptionType type = OptionType::Call;

    [[nodiscard]] double operator()(double terminal_spot) const noexcept {
        const double phi = type == OptionType::Call ? 1.0 : -1.0;
        return std::max(phi * (terminal_spot - strike), 0.0);
    }
};

static_assert(Payoff<EuropeanPayoff>);

} // namespace mcd::payoffs
