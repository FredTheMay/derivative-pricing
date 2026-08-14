#pragma once

#include "mcd/core/types.hpp"
#include "mcd/payoffs/path_payoff.hpp"

#include <algorithm>

namespace mcd::payoffs {

// Discretely-monitored single barrier. Rebate is paid at expiry (a
// documented simplification -- see docs/design/03-exotics-variance-
// reduction.md sec.7 item 1), discounted once alongside the vanilla payoff
// by the caller.
struct BarrierPayoff {
    double strike;
    double barrier;
    double rebate;
    OptionType type;
    BarrierDirection direction;
    BarrierKnock knock;

    bool breached = false;
    double last_price = 0.0;

    void observe(double price) noexcept {
        const bool crossed = direction == BarrierDirection::Up ? price >= barrier
                                                                 : price <= barrier;
        breached = breached || crossed;
        last_price = price;
    }

    [[nodiscard]] double result() const noexcept {
        const double phi = type == OptionType::Call ? 1.0 : -1.0;
        const double vanilla = std::max(phi * (last_price - strike), 0.0);
        const bool pays_vanilla = knock == BarrierKnock::Out ? !breached : breached;
        return pays_vanilla ? vanilla : rebate;
    }
};

static_assert(PathPayoff<BarrierPayoff>);

} // namespace mcd::payoffs
