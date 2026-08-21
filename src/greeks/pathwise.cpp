#include "mcd/greeks/pathwise.hpp"

#include "mcd/core/rng.hpp"
#include "mcd/core/stats.hpp"
#include "mcd/models/gbm.hpp"

#include <cmath>

namespace mcd::greeks {

namespace {

struct Accumulators {
    WelfordAccumulator delta, vega, rho;
};

PathwiseGreeksResult finish(const WelfordAccumulator& acc) noexcept {
    return PathwiseGreeksResult{.value = acc.mean(), .standard_error = acc.standard_error()};
}

} // namespace

// Pathwise European: differentiates the payoff along the path, not the density
// (docs/design/09-pathwise-greeks.md sec.2). ∂S_T/∂S_0 = S_T/S_0, ∂S_T/∂σ =
// S_T·(√T·Z − σT), ∂S_T/∂r = S_T·T -- all closed forms of GBM's exact solution. rho gets
// the same discount-factor product-rule correction term LR's rho needed (see
// src/greeks/likelihood_ratio.cpp), for the same reason.
PathwiseGreeks pathwise_european(double spot, double strike, double rate, double carry_yield,
                                  double vol, double time, OptionType type,
                                  std::uint64_t path_count, std::uint64_t seed) noexcept {
    const double discount = std::exp(-rate * time);
    const double sqrt_t = std::sqrt(time);
    const double phi = type == OptionType::Call ? 1.0 : -1.0;
    const models::GbmParams params{.spot = spot, .rate = rate, .carry_yield = carry_yield,
                                    .vol = vol, .time = time};

    Accumulators acc;
    for (std::uint64_t path_index = 0; path_index < path_count; ++path_index) {
        const double z = standard_normal_variate(seed, path_index);
        const double s_t = models::gbm_terminal_spot(params, z);
        const bool in_the_money = phi * (s_t - strike) > 0.0;
        const double slope = in_the_money ? phi : 0.0;
        const double h = discount * std::max(phi * (s_t - strike), 0.0);

        const double ds_dspot = s_t / spot;
        const double ds_dvol = s_t * (sqrt_t * z - vol * time);
        const double ds_drate = s_t * time;

        acc.delta.add(discount * slope * ds_dspot);
        acc.vega.add(discount * slope * ds_dvol);
        acc.rho.add(discount * slope * ds_drate - time * h);
    }
    return PathwiseGreeks{.delta = finish(acc.delta), .vega = finish(acc.vega),
                           .rho = finish(acc.rho)};
}

// Pathwise Asian (arithmetic/geometric, fixed/floating). The path average's pathwise
// sensitivity is a per-step sum: ∂S_i/∂S_0 = S_i/S_0 for every step (GBM is
// multiplicative, so this holds regardless of step index); ∂ln(S_i)/∂σ =
// -σ·i·dt + √dt·(Z_1+...+Z_i) and ∂ln(S_i)/∂r = i·dt accumulate across steps the same
// way vega/rho's LR path-dependent scores summed across steps
// (docs/design/08-likelihood-ratio-greeks.md sec.4) -- same underlying reason (the
// parameter enters every step's transition, not just the first).
PathwiseGreeks pathwise_asian(double spot, double strike, double rate, double carry_yield,
                               double vol, double time, OptionType type,
                               StrikeStyle strike_style, AverageStyle average_style,
                               int monitoring_points, std::uint64_t path_count,
                               std::uint64_t seed) noexcept {
    const double dt = time / static_cast<double>(monitoring_points);
    const double sqrt_dt = std::sqrt(dt);
    const double discount = std::exp(-rate * time);
    const double phi = type == OptionType::Call ? 1.0 : -1.0;
    const auto n = static_cast<double>(monitoring_points);

    Accumulators acc;

    for (std::uint64_t path_index = 0; path_index < path_count; ++path_index) {
        double s = spot;
        double cum_z = 0.0;
        double sum_s = 0.0, log_sum = 0.0;
        double sum_s_dvol = 0.0, sum_dlns_dvol = 0.0;
        double sum_s_drate = 0.0, sum_dlns_drate = 0.0;
        double last_s_dvol = 0.0, last_s_drate = 0.0;

        for (int step = 0; step < monitoring_points; ++step) {
            const double z =
                standard_normal_variate(seed, path_index, static_cast<std::uint32_t>(step));
            cum_z += z;
            const models::GbmParams step_params{.spot = s, .rate = rate,
                                                  .carry_yield = carry_yield, .vol = vol,
                                                  .time = dt};
            s = models::gbm_terminal_spot(step_params, z);

            const auto i = static_cast<double>(step + 1);
            const double dlns_dvol = -vol * i * dt + sqrt_dt * cum_z;
            const double dlns_drate = i * dt;
            const double ds_dvol = s * dlns_dvol;
            const double ds_drate = s * dlns_drate;

            sum_s += s;
            log_sum += std::log(s);
            sum_s_dvol += ds_dvol;
            sum_dlns_dvol += dlns_dvol;
            sum_s_drate += ds_drate;
            sum_dlns_drate += dlns_drate;
            last_s_dvol = ds_dvol;
            last_s_drate = ds_drate;
        }

        const double avg = average_style == AverageStyle::Arithmetic
                                ? sum_s / n
                                : std::exp(log_sum / n);
        const double d_avg_dspot = avg / spot;
        const double d_avg_dvol = average_style == AverageStyle::Arithmetic
                                       ? sum_s_dvol / n
                                       : avg * (sum_dlns_dvol / n);
        const double d_avg_drate = average_style == AverageStyle::Arithmetic
                                        ? sum_s_drate / n
                                        : avg * (sum_dlns_drate / n);
        const double d_slast_dspot = s / spot;
        const double d_slast_dvol = last_s_dvol;
        const double d_slast_drate = last_s_drate;

        const double reference =
            strike_style == StrikeStyle::Fixed ? (avg - strike) : (s - avg);
        const bool in_the_money = phi * reference > 0.0;
        const double slope = in_the_money ? phi : 0.0;
        const double h = discount * std::max(phi * reference, 0.0);

        double delta_raw = 0.0, vega_raw = 0.0, rate_raw = 0.0;
        if (strike_style == StrikeStyle::Fixed) {
            delta_raw = slope * d_avg_dspot;
            vega_raw = slope * d_avg_dvol;
            rate_raw = slope * d_avg_drate;
        } else {
            delta_raw = slope * (d_slast_dspot - d_avg_dspot);
            vega_raw = slope * (d_slast_dvol - d_avg_dvol);
            rate_raw = slope * (d_slast_drate - d_avg_drate);
        }

        acc.delta.add(discount * delta_raw);
        acc.vega.add(discount * vega_raw);
        acc.rho.add(discount * rate_raw - time * h);
    }
    return PathwiseGreeks{.delta = finish(acc.delta), .vega = finish(acc.vega),
                           .rho = finish(acc.rho)};
}

// Deliberately broken -- see the header comment and
// docs/design/09-pathwise-greeks.md sec.3. A cash-or-nothing digital's payoff is
// piecewise *constant*: its pathwise slope is 0 everywhere except exactly at the strike
// (probability zero), so this converges to exactly 0.0 regardless of path count -- not
// noisy, wrong.
PathwiseGreeksResult pathwise_digital_delta_naive_and_broken(
    double spot, double strike, double rate, double carry_yield, double vol, double time,
    OptionType type, double cash_amount, std::uint64_t path_count, std::uint64_t seed) noexcept {
    const double discount = std::exp(-rate * time);
    const double phi = type == OptionType::Call ? 1.0 : -1.0;
    const models::GbmParams params{.spot = spot, .rate = rate, .carry_yield = carry_yield,
                                    .vol = vol, .time = time};

    WelfordAccumulator acc;
    for (std::uint64_t path_index = 0; path_index < path_count; ++path_index) {
        const double z = standard_normal_variate(seed, path_index);
        const double s_t = models::gbm_terminal_spot(params, z);
        // Real cash-or-nothing payoff, computed for realism -- but its slope w.r.t. S_T
        // is 0 at every path regardless of the payoff's own value: the payoff is
        // piecewise *constant* (cash_amount or 0), and its only nonzero slope is a Dirac
        // delta exactly at the strike, a measure-zero event this Monte Carlo path never
        // lands on exactly. That's the failure itself, not a bug: every path
        // contributes exactly 0, no matter how deep in or out of the money it is.
        const bool in_the_money = phi * (s_t - strike) > 0.0;
        const double h = discount * (in_the_money ? cash_amount : 0.0);
        const double slope = 0.0;
        acc.add(slope * h);
    }
    return finish(acc);
}

} // namespace mcd::greeks
