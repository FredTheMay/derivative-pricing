#include "mcd/pricers/monte_carlo.hpp"

#include "mcd/core/rng.hpp"
#include "mcd/core/stats.hpp"
#include "mcd/models/gbm.hpp"
#include "mcd/payoffs/european.hpp"

#include <cmath>

namespace mcd::pricers {

McResult monte_carlo_european(double spot, double strike, double rate, double carry_yield,
                               double vol, double time, OptionType type,
                               std::uint64_t path_count, std::uint64_t seed) noexcept {
    const models::GbmParams params{.spot = spot, .rate = rate, .carry_yield = carry_yield,
                                    .vol = vol, .time = time};
    const payoffs::EuropeanPayoff payoff{.strike = strike, .type = type};
    const double discount = std::exp(-rate * time);

    WelfordAccumulator accumulator;
    for (std::uint64_t path_index = 0; path_index < path_count; ++path_index) {
        const double z = standard_normal_variate(seed, path_index);
        const double terminal_spot = models::gbm_terminal_spot(params, z);
        const double discounted_payoff = discount * payoff(terminal_spot);
        accumulator.add(discounted_payoff);
    }

    return McResult{.price = accumulator.mean(), .standard_error = accumulator.standard_error(),
                     .path_count = accumulator.count()};
}

} // namespace mcd::pricers
