#include "mcd/pricers/analytic.hpp"

#include "mcd/core/normal.hpp"

#include <cmath>

namespace mcd::pricers {

namespace {

constexpr double kCostOfCarryEpsilon = 1e-12;

double sign(OptionType type) noexcept { return type == OptionType::Call ? 1.0 : -1.0; }

struct D1D2 {
    double d1;
    double d2;
};

// d1/d2 for a BSM-style price with carry yield q, i.e. cost of carry b = r - q.
D1D2 bsm_d1d2(double spot, double strike, double rate, double carry_yield, double vol,
              double time) noexcept {
    const double b = rate - carry_yield;
    const double sqrt_t = std::sqrt(time);
    const double d1 = (std::log(spot / strike) + (b + 0.5 * vol * vol) * time) / (vol * sqrt_t);
    return {.d1 = d1, .d2 = d1 - vol * sqrt_t};
}

} // namespace

double black_scholes_merton(double spot, double strike, double rate, double carry_yield,
                             double vol, double time, OptionType type) noexcept {
    const auto [d1, d2] = bsm_d1d2(spot, strike, rate, carry_yield, vol, time);
    const double phi = sign(type);
    const double df_carry = std::exp(-carry_yield * time);
    const double df_rate = std::exp(-rate * time);
    return phi * spot * df_carry * standard_normal_cdf(phi * d1) -
           phi * strike * df_rate * standard_normal_cdf(phi * d2);
}

double black76(double forward, double strike, double rate, double vol, double time,
               OptionType type) noexcept {
    const double sqrt_t = std::sqrt(time);
    const double d1 = (std::log(forward / strike) + 0.5 * vol * vol * time) / (vol * sqrt_t);
    const double d2 = d1 - vol * sqrt_t;
    const double phi = sign(type);
    const double df = std::exp(-rate * time);
    return phi * df * (forward * standard_normal_cdf(phi * d1) -
                        strike * standard_normal_cdf(phi * d2));
}

double kemna_vorst(double spot, double strike, double rate, double carry_yield, double vol,
                    double time, OptionType type) noexcept {
    // Continuous geometric average of GBM is lognormal with adjusted vol sigma/sqrt(3)
    // and adjusted cost of carry b_g = 0.5*(r - q - sigma^2/6); price via BSM with those.
    const double sigma_g = vol / std::sqrt(3.0);
    const double b_g = 0.5 * (rate - carry_yield - vol * vol / 6.0);
    const double q_eff = rate - b_g;
    return black_scholes_merton(spot, strike, rate, q_eff, sigma_g, time, type);
}

double reiner_rubinstein(double spot, double strike, double barrier, double rate,
                          double carry_yield, double vol, double time, OptionType type,
                          BarrierDirection direction, BarrierKnock knock,
                          double rebate) noexcept {
    const double b = rate - carry_yield;
    const double sqrt_t = std::sqrt(time);
    const double mu = (b - 0.5 * vol * vol) / (vol * vol);
    const double lambda = std::sqrt(mu * mu + 2.0 * rate / (vol * vol));

    const double phi = sign(type);
    const double eta = direction == BarrierDirection::Down ? 1.0 : -1.0;

    const double x1 = std::log(spot / strike) / (vol * sqrt_t) + (1.0 + mu) * vol * sqrt_t;
    const double x2 = std::log(spot / barrier) / (vol * sqrt_t) + (1.0 + mu) * vol * sqrt_t;
    const double y1 =
        std::log(barrier * barrier / (spot * strike)) / (vol * sqrt_t) + (1.0 + mu) * vol * sqrt_t;
    const double y2 = std::log(barrier / spot) / (vol * sqrt_t) + (1.0 + mu) * vol * sqrt_t;
    const double z = std::log(barrier / spot) / (vol * sqrt_t) + lambda * vol * sqrt_t;

    const double df_carry = std::exp((b - rate) * time);
    const double df_rate = std::exp(-rate * time);
    const double hs_2mu1 = std::pow(barrier / spot, 2.0 * (mu + 1.0));
    const double hs_2mu = std::pow(barrier / spot, 2.0 * mu);

    const double A = phi * spot * df_carry * standard_normal_cdf(phi * x1) -
                      phi * strike * df_rate * standard_normal_cdf(phi * x1 - phi * vol * sqrt_t);
    const double B = phi * spot * df_carry * standard_normal_cdf(phi * x2) -
                      phi * strike * df_rate * standard_normal_cdf(phi * x2 - phi * vol * sqrt_t);
    const double C = phi * spot * df_carry * hs_2mu1 * standard_normal_cdf(eta * y1) -
                      phi * strike * df_rate * hs_2mu *
                          standard_normal_cdf(eta * y1 - eta * vol * sqrt_t);
    const double D = phi * spot * df_carry * hs_2mu1 * standard_normal_cdf(eta * y2) -
                      phi * strike * df_rate * hs_2mu *
                          standard_normal_cdf(eta * y2 - eta * vol * sqrt_t);
    const double E =
        rebate * df_rate *
        (standard_normal_cdf(eta * x2 - eta * vol * sqrt_t) -
         hs_2mu * standard_normal_cdf(eta * y2 - eta * vol * sqrt_t));
    const double F =
        rebate * (std::pow(barrier / spot, mu + lambda) * standard_normal_cdf(eta * z) +
                  std::pow(barrier / spot, mu - lambda) *
                      standard_normal_cdf(eta * z - 2.0 * eta * lambda * vol * sqrt_t));

    const bool strike_above_barrier = strike > barrier;

    if (knock == BarrierKnock::In) {
        if (direction == BarrierDirection::Down) {
            if (type == OptionType::Call) {
                return strike_above_barrier ? (C + E) : (A - B + D + E);
            }
            return strike_above_barrier ? (B - C + D + E) : (A + E);
        }
        // Up-and-in
        if (type == OptionType::Call) {
            return strike_above_barrier ? (A + E) : (B - C + D + E);
        }
        return strike_above_barrier ? (A - B + D + E) : (C + E);
    }

    // Knock-out
    if (direction == BarrierDirection::Down) {
        if (type == OptionType::Call) {
            return strike_above_barrier ? (A - C + F) : (B - D + F);
        }
        return strike_above_barrier ? (A - B + C - D + F) : F;
    }
    // Up-and-out
    if (type == OptionType::Call) {
        return strike_above_barrier ? F : (A - B + C - D + F);
    }
    return strike_above_barrier ? (B - D + F) : (A - C + F);
}

double lookback_fixed_strike(double spot, double strike, double rate, double carry_yield,
                              double vol, double time, OptionType type) noexcept {
    const double b = rate - carry_yield;
    const double sqrt_t = std::sqrt(time);
    const double df_rate = std::exp(-rate * time);
    const double df_carry = std::exp((b - rate) * time);

    if (type == OptionType::Call) {
        if (strike < spot) {
            // Running max already exceeds K: guaranteed (S-K) plus a lookback struck at S.
            return (spot - strike) * df_rate +
                   lookback_fixed_strike(spot, spot, rate, carry_yield, vol, time, type);
        }
        const double d1 = (std::log(spot / strike) + (b + 0.5 * vol * vol) * time) / (vol * sqrt_t);
        const double term = std::abs(b) < kCostOfCarryEpsilon
                                 ? spot * df_rate * vol * sqrt_t *
                                       (standard_normal_pdf(d1) + d1 * standard_normal_cdf(d1))
                                 : spot * df_rate * (vol * vol / (2.0 * b)) *
                                       (-std::pow(spot / strike, -2.0 * b / (vol * vol)) *
                                            standard_normal_cdf(d1 - 2.0 * b * sqrt_t / vol) +
                                        std::exp(b * time) * standard_normal_cdf(d1));
        return spot * df_carry * standard_normal_cdf(d1) -
               strike * df_rate * standard_normal_cdf(d1 - vol * sqrt_t) + term;
    }

    if (strike > spot) {
        return (strike - spot) * df_rate +
               lookback_fixed_strike(spot, spot, rate, carry_yield, vol, time, type);
    }
    const double e1 =
        (std::log(spot / strike) + (-b + 0.5 * vol * vol) * time) / (vol * sqrt_t);
    const double e2 = e1 - vol * sqrt_t;
    const double term = std::abs(b) < kCostOfCarryEpsilon
                             ? spot * df_rate * vol * sqrt_t *
                                   (standard_normal_pdf(e1) + e1 * standard_normal_cdf(-e1))
                             : spot * df_rate * (vol * vol / (2.0 * b)) *
                                   (std::pow(spot / strike, -2.0 * b / (vol * vol)) *
                                        standard_normal_cdf(-e1 + 2.0 * b * sqrt_t / vol) -
                                    std::exp(b * time) * standard_normal_cdf(-e1));
    return strike * df_rate * standard_normal_cdf(-e2) -
           spot * df_carry * standard_normal_cdf(-e1) + term;
}

double lookback_floating_strike(double spot, double rate, double carry_yield, double vol,
                                 double time, OptionType type) noexcept {
    const double b = rate - carry_yield;
    const double sqrt_t = std::sqrt(time);
    const double df_rate = std::exp(-rate * time);
    const double df_carry = std::exp((b - rate) * time);

    if (type == OptionType::Call) {
        const double a1 = ((b + 0.5 * vol * vol) * time) / (vol * sqrt_t);
        const double a2 = a1 - vol * sqrt_t;
        const double term = std::abs(b) < kCostOfCarryEpsilon
                                 ? spot * df_rate * vol * sqrt_t *
                                       (standard_normal_pdf(a1) + a1 * standard_normal_cdf(a1))
                                 : spot * df_rate * (vol * vol / (2.0 * b)) *
                                       (std::exp(b * time) * standard_normal_cdf(a1) -
                                        standard_normal_cdf(a1 - 2.0 * b * sqrt_t / vol));
        return spot * df_carry * standard_normal_cdf(a1) - spot * df_rate * standard_normal_cdf(a2) +
               term;
    }

    const double a1 = ((-b + 0.5 * vol * vol) * time) / (vol * sqrt_t);
    const double a2 = a1 - vol * sqrt_t;
    const double term = std::abs(b) < kCostOfCarryEpsilon
                             ? spot * df_rate * vol * sqrt_t *
                                   (standard_normal_pdf(a1) + a1 * standard_normal_cdf(-a1))
                             : spot * df_rate * (vol * vol / (2.0 * b)) *
                                   (standard_normal_cdf(-a1 + 2.0 * b * sqrt_t / vol) -
                                    std::exp(b * time) * standard_normal_cdf(-a1));
    return spot * df_rate * standard_normal_cdf(-a2) - spot * df_carry * standard_normal_cdf(-a1) +
           term;
}

double digital(double spot, double strike, double rate, double carry_yield, double vol,
               double time, OptionType type, DigitalStyle style, double cash_amount) noexcept {
    const auto [d1, d2] = bsm_d1d2(spot, strike, rate, carry_yield, vol, time);
    const double phi = sign(type);
    if (style == DigitalStyle::CashOrNothing) {
        return cash_amount * std::exp(-rate * time) * standard_normal_cdf(phi * d2);
    }
    return spot * std::exp(-carry_yield * time) * standard_normal_cdf(phi * d1);
}

double forward_price(double spot, double rate, double carry_yield, double time) noexcept {
    return spot * std::exp((rate - carry_yield) * time);
}

double forward_value(double forward_now, double forward_at_inception, double rate,
                      double time_to_expiry) noexcept {
    return (forward_now - forward_at_inception) * std::exp(-rate * time_to_expiry);
}

double futures_mark_to_market(double futures_price_new, double futures_price_old) noexcept {
    return futures_price_new - futures_price_old;
}

} // namespace mcd::pricers
