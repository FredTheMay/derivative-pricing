#include "mcd/greeks/likelihood_ratio.hpp"

#include "mcd/core/rng.hpp"
#include "mcd/core/stats.hpp"
#include "mcd/models/gbm.hpp"
#include "mcd/payoffs/barrier.hpp"
#include "mcd/payoffs/digital.hpp"
#include "mcd/payoffs/european.hpp"

#include <cmath>

namespace mcd::greeks {

namespace {

struct TerminalAccumulators {
    WelfordAccumulator delta, gamma, vega, theta, rho;
};

struct BarrierAccumulators {
    WelfordAccumulator delta, gamma, vega, rho;
};

LrGreeksResult finish(const WelfordAccumulator& acc) noexcept {
    return LrGreeksResult{.value = acc.mean(), .standard_error = acc.standard_error()};
}

// Terminal-payoff (European/digital) LR Greeks: a single Z draw per path. Score functions
// derived in docs/design/08-likelihood-ratio-greeks.md sec.3 -- delta/gamma/vega match the
// published Broadie-Glasserman (1996) results; rho/theta include the extra terms from
// differentiating the discount factor e^{-rT} itself (product rule), since this project's
// LR estimator reports the fully discounted price sensitivity, not just the density score.
template <payoffs::Payoff P>
LrGreeks likelihood_ratio_terminal(double spot, double rate, double carry_yield, double vol,
                                    double time, const P& payoff, std::uint64_t path_count,
                                    std::uint64_t seed) noexcept {
    const double discount = std::exp(-rate * time);
    const double sqrt_t = std::sqrt(time);
    const double mu = rate - carry_yield - 0.5 * vol * vol;

    TerminalAccumulators acc;
    const models::GbmParams params{.spot = spot, .rate = rate, .carry_yield = carry_yield,
                                    .vol = vol, .time = time};

    for (std::uint64_t path_index = 0; path_index < path_count; ++path_index) {
        const double z = standard_normal_variate(seed, path_index);
        const double s_t = models::gbm_terminal_spot(params, z);
        const double h = discount * payoff(s_t);

        const double delta_score = z / (spot * vol * sqrt_t);
        const double gamma_score =
            (z * z - vol * sqrt_t * z - 1.0) / (spot * spot * vol * vol * time);
        const double vega_score = (z * z - 1.0) / vol - sqrt_t * z;
        // Raw density scores (before the discount-factor correction terms below).
        const double rho_score_raw = sqrt_t * z / vol;
        const double theta_score_raw = (z * z - 1.0) / (2.0 * time) + mu * z / (vol * sqrt_t);

        acc.delta.add(h * delta_score);
        acc.gamma.add(h * gamma_score);
        acc.vega.add(h * vega_score);
        // d/dr[e^{-rT}E[h]] = -T*e^{-rT}E[h] + e^{-rT}E[h*score] -- per-path estimator:
        // h*(rho_score_raw - T), since h already carries the discount factor.
        acc.rho.add(h * (rho_score_raw - time));
        // theta = -d/dT[e^{-rT}E[h]] (this project's decay convention, Phase 5) =
        // -(-r*e^{-rT}E[h] + e^{-rT}E[h*score]) = e^{-rT}E[h*(r - score)], per-path:
        // h*(rate - theta_score_raw).
        acc.theta.add(h * (rate - theta_score_raw));
    }

    return LrGreeks{.delta = finish(acc.delta), .gamma = finish(acc.gamma),
                     .vega = finish(acc.vega), .rho = finish(acc.rho),
                     .theta = finish(acc.theta)};
}

} // namespace

LrGreeks likelihood_ratio_european(double spot, double strike, double rate, double carry_yield,
                                    double vol, double time, OptionType type,
                                    std::uint64_t path_count, std::uint64_t seed) noexcept {
    const payoffs::EuropeanPayoff payoff{.strike = strike, .type = type};
    return likelihood_ratio_terminal(spot, rate, carry_yield, vol, time, payoff, path_count,
                                      seed);
}

LrGreeks likelihood_ratio_digital(double spot, double strike, double rate, double carry_yield,
                                   double vol, double time, OptionType type, DigitalStyle style,
                                   double cash_amount, std::uint64_t path_count,
                                   std::uint64_t seed) noexcept {
    const payoffs::DigitalPayoff payoff{.strike = strike, .type = type, .style = style,
                                         .cash_amount = cash_amount};
    return likelihood_ratio_terminal(spot, rate, carry_yield, vol, time, payoff, path_count,
                                      seed);
}

// Path-dependent (barrier) LR Greeks. The path's joint density factors as a product of
// per-step transition densities, so its log-score is a sum over steps
// (docs/design/08-likelihood-ratio-greeks.md sec.4):
//  - delta/gamma: only the *first* step's transition density depends on S_0 (later steps
//    condition on S_1, S_2, ... -- not on S_0 directly), so these scores use exactly the
//    terminal-case formulas with T -> dt and Z -> Z_1 (the first step's draw only).
//  - vega/rho: vol and rate appear in *every* step's density (via each step's own
//    diffusion/drift), so these scores sum the terminal-case per-step formula (with
//    T -> dt, Z -> Z_i) across every monitoring date.
//  - theta: deliberately not computed here -- see the LrGreeks::theta comment.
LrGreeks likelihood_ratio_barrier(double spot, double strike, double barrier, double rate,
                                   double carry_yield, double vol, double time, OptionType type,
                                   BarrierDirection direction, BarrierKnock knock, double rebate,
                                   int monitoring_points, std::uint64_t path_count,
                                   std::uint64_t seed) noexcept {
    const double dt = time / static_cast<double>(monitoring_points);
    const double sqrt_dt = std::sqrt(dt);
    const double discount = std::exp(-rate * time);

    BarrierAccumulators acc;

    for (std::uint64_t path_index = 0; path_index < path_count; ++path_index) {
        payoffs::BarrierPayoff payoff{.strike = strike, .barrier = barrier, .rebate = rebate,
                                       .type = type, .direction = direction, .knock = knock};
        double s = spot;
        double z1 = 0.0;
        double vega_score_sum = 0.0;
        double rho_score_sum = 0.0;

        for (int step = 0; step < monitoring_points; ++step) {
            const double z =
                standard_normal_variate(seed, path_index, static_cast<std::uint32_t>(step));
            if (step == 0) {
                z1 = z;
            }
            vega_score_sum += (z * z - 1.0) / vol - sqrt_dt * z;
            rho_score_sum += sqrt_dt * z / vol;

            const models::GbmParams step_params{.spot = s, .rate = rate,
                                                  .carry_yield = carry_yield, .vol = vol,
                                                  .time = dt};
            s = models::gbm_terminal_spot(step_params, z);
            payoff.observe(s);
        }

        const double h = discount * payoff.result();

        const double delta_score = z1 / (spot * vol * sqrt_dt);
        const double gamma_score =
            (z1 * z1 - vol * sqrt_dt * z1 - 1.0) / (spot * spot * vol * vol * dt);

        acc.delta.add(h * delta_score);
        acc.gamma.add(h * gamma_score);
        acc.vega.add(h * vega_score_sum);
        acc.rho.add(h * (rho_score_sum - time));
    }

    return LrGreeks{.delta = finish(acc.delta), .gamma = finish(acc.gamma),
                     .vega = finish(acc.vega), .rho = finish(acc.rho), .theta = std::nullopt};
}

} // namespace mcd::greeks
