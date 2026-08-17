#include "mcd/pricers/binomial.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace mcd::pricers {

BinomialResult crr_binomial(double spot, double strike, double rate, double carry_yield,
                             double vol, double time, int steps, OptionType type) noexcept {
    const double dt = time / static_cast<double>(steps);
    const double u = std::exp(vol * std::sqrt(dt));
    const double d = 1.0 / u;
    const double growth = std::exp((rate - carry_yield) * dt);
    const double pi = (growth - d) / (u - d);
    const double discount = std::exp(-rate * dt);
    const double phi = type == OptionType::Call ? 1.0 : -1.0;

    const auto node_count = static_cast<std::size_t>(steps) + 1;
    std::vector<double> values(node_count);
    for (std::size_t i = 0; i < node_count; ++i) {
        const auto up_moves = static_cast<double>(i);
        const double down_moves = static_cast<double>(steps) - up_moves;
        const double terminal_spot = spot * std::pow(u, up_moves) * std::pow(d, down_moves);
        values[i] = std::max(phi * (terminal_spot - strike), 0.0);
    }

    for (int level = steps - 1; level >= 0; --level) {
        const auto count = static_cast<std::size_t>(level) + 1;
        for (std::size_t i = 0; i < count; ++i) {
            values[i] = discount * (pi * values[i + 1] + (1.0 - pi) * values[i]);
        }
    }

    return BinomialResult{.price = values[0], .risk_neutral_probability = pi, .up_factor = u,
                           .down_factor = d};
}

double crr_binomial_american(double spot, double strike, double rate, double carry_yield,
                              double vol, double time, int steps, OptionType type) noexcept {
    const double dt = time / static_cast<double>(steps);
    const double u = std::exp(vol * std::sqrt(dt));
    const double d = 1.0 / u;
    const double growth = std::exp((rate - carry_yield) * dt);
    const double pi = (growth - d) / (u - d);
    const double discount = std::exp(-rate * dt);
    const double phi = type == OptionType::Call ? 1.0 : -1.0;

    const auto node_count = static_cast<std::size_t>(steps) + 1;
    std::vector<double> values(node_count);
    std::vector<double> spot_at(node_count);
    for (std::size_t i = 0; i < node_count; ++i) {
        const auto up_moves = static_cast<double>(i);
        const double down_moves = static_cast<double>(steps) - up_moves;
        spot_at[i] = spot * std::pow(u, up_moves) * std::pow(d, down_moves);
        values[i] = std::max(phi * (spot_at[i] - strike), 0.0);
    }

    for (int level = steps - 1; level >= 0; --level) {
        const auto count = static_cast<std::size_t>(level) + 1;
        for (std::size_t i = 0; i < count; ++i) {
            const double continuation = discount * (pi * values[i + 1] + (1.0 - pi) * values[i]);
            const auto up_moves = static_cast<double>(i);
            const double down_moves = static_cast<double>(level) - up_moves;
            const double node_spot = spot * std::pow(u, up_moves) * std::pow(d, down_moves);
            const double exercise = std::max(phi * (node_spot - strike), 0.0);
            values[i] = std::max(continuation, exercise);
        }
    }

    return values[0];
}

} // namespace mcd::pricers
