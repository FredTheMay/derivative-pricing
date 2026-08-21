#include "mcd/pricers/qmc.hpp"

#include "mcd/core/normal.hpp"
#include "mcd/core/sobol.hpp"
#include "mcd/models/brownian_bridge_path.hpp"
#include "mcd/models/gbm.hpp"
#include "mcd/payoffs/asian.hpp"
#include "mcd/payoffs/european.hpp"

#include <array>
#include <cmath>

namespace mcd::pricers {

QmcResult qmc_sobol_european(double spot, double strike, double rate, double carry_yield,
                              double vol, double time, OptionType type,
                              std::uint64_t path_count) noexcept {
    const models::GbmParams params{
        .spot = spot, .rate = rate, .carry_yield = carry_yield, .vol = vol, .time = time};
    const payoffs::EuropeanPayoff payoff{.strike = strike, .type = type};
    const double discount = std::exp(-rate * time);

    double sum = 0.0;
    for (std::uint64_t index = 1; index <= path_count; ++index) {
        const double u = sobol_point(0, index);
        const double z = inverse_standard_normal_cdf(u);
        sum += discount * payoff(models::gbm_terminal_spot(params, z));
    }
    return QmcResult{.price = sum / static_cast<double>(path_count)};
}

QmcResult qmc_sobol_asian(double spot, double strike, double rate, double carry_yield, double vol,
                           double time, OptionType type, StrikeStyle strike_style,
                           int monitoring_points, std::uint64_t path_count) noexcept {
    const models::GbmParams params{
        .spot = spot, .rate = rate, .carry_yield = carry_yield, .vol = vol, .time = time};
    const double discount = std::exp(-rate * time);
    const int n = monitoring_points;

    double sum = 0.0;
    std::array<double, kSobolMaxDimensions> normals{};
    for (std::uint64_t index = 1; index <= path_count; ++index) {
        for (int d = 0; d < n; ++d) {
            const double u = sobol_point(static_cast<unsigned>(d), index);
            normals[static_cast<std::size_t>(d)] = inverse_standard_normal_cdf(u);
        }
        const auto spots = models::brownian_bridge_gbm_path(
            params, n, std::span<const double>(normals.data(), static_cast<std::size_t>(n)));

        payoffs::ArithmeticAsianPayoff payoff{.strike = strike, .type = type, .style = strike_style};
        for (const double s : spots) {
            payoff.observe(s);
        }
        sum += discount * payoff.result();
    }
    return QmcResult{.price = sum / static_cast<double>(path_count)};
}

} // namespace mcd::pricers
