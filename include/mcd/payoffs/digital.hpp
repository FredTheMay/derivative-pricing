#pragma once

#include "mcd/core/types.hpp"
#include "mcd/payoffs/european.hpp"

namespace mcd::payoffs {

// Digital / binary option. Terminal-spot only, so it uses the simple Payoff
// concept (european.hpp), not PathPayoff.
struct DigitalPayoff {
    double strike;
    OptionType type;
    DigitalStyle style;
    double cash_amount = 1.0;

    [[nodiscard]] double operator()(double terminal_spot) const noexcept {
        const double phi = type == OptionType::Call ? 1.0 : -1.0;
        const bool in_the_money = phi * (terminal_spot - strike) > 0.0;
        if (!in_the_money) {
            return 0.0;
        }
        return style == DigitalStyle::CashOrNothing ? cash_amount : terminal_spot;
    }
};

static_assert(Payoff<DigitalPayoff>);

} // namespace mcd::payoffs
