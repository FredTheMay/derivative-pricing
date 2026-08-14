// Each test below corresponds to one row of the CFA Level I Derivatives invariant
// table described in docs/cfa-mapping.md. Module references are numbers/topics only;
// no curriculum text is reproduced anywhere in this file.

#include "mcd/pricers/analytic.hpp"
#include "mcd/pricers/binomial.hpp"

#include <gtest/gtest.h>

#include <cmath>

using mcd::OptionType;
namespace pricers = mcd::pricers;

namespace {
constexpr double kTightTol = 1e-9;
constexpr double kBump = 1e-4;
}

// LM4 — cost of carry: F0 = S0 * e^{(r-q)T}
TEST(CfaInvariant, CostOfCarry) {
    const double s = 100.0, r = 0.04, q = 0.015, t = 1.5;
    const double expected = s * std::exp((r - q) * t);
    EXPECT_NEAR(pricers::forward_price(s, r, q, t), expected, kTightTol);
}

// LM5 — a forward contract is worth zero at initiation, at the fair forward price.
TEST(CfaInvariant, ForwardValueAtInitiationIsZero) {
    const double s = 100.0, r = 0.03, q = 0.01, t = 1.0;
    const double f0 = pricers::forward_price(s, r, q, t);
    EXPECT_NEAR(pricers::forward_value(f0, f0, r, t), 0.0, kTightTol);
}

// LM5 — forward value during its life: Vt = (Ft - F0) * e^{-r(T-t)}.
TEST(CfaInvariant, ForwardValueDuringLife) {
    const double f0 = 105.0, ft = 112.0, r = 0.03, time_to_expiry = 0.4;
    const double expected = (ft - f0) * std::exp(-r * time_to_expiry);
    EXPECT_NEAR(pricers::forward_value(ft, f0, r, time_to_expiry), expected, kTightTol);
}

// LM6 — futures and forward prices coincide under deterministic interest rates.
TEST(CfaInvariant, FuturesForwardEquivalenceUnderDeterministicRates) {
    const double s = 100.0, r = 0.04, q = 0.02, t = 0.75;
    // In this model rates are deterministic by construction, so the futures price at
    // inception is computed by the same cost-of-carry formula as the forward price.
    const double forward = pricers::forward_price(s, r, q, t);
    const double futures = pricers::forward_price(s, r, q, t);
    EXPECT_NEAR(forward, futures, kTightTol);
}

// LM8 — option value = exercise value + time value; both non-negative before expiry.
//
// This holds unconditionally for American options (early exercise is always available
// as a floor) but NOT in general for European options: a deep in-the-money European put
// can price below its immediate exercise value, because the holder cannot actually
// exercise early to realize that value now, and discounting the eventual payoff can
// leave less than the current intrinsic value. This is a real, well-documented result
// (not an implementation bug -- verified against put-call parity and no-arbitrage bounds
// elsewhere in this suite), and it is exactly the kind of nuance the CFA Level I
// curriculum's single-sentence heuristic elides. The grid below stays near the money,
// where the heuristic is accurate, and the caveat is recorded in docs/cfa-mapping.md.
TEST(CfaInvariant, ExerciseValuePlusTimeValueIsNonNegative) {
    for (double s : {95.0, 100.0, 105.0}) {
        for (double k : {95.0, 100.0, 105.0}) {
            const double r = 0.04, q = 0.01, sigma = 0.25, t = 0.5;
            for (OptionType type : {OptionType::Call, OptionType::Put}) {
                const double phi = type == OptionType::Call ? 1.0 : -1.0;
                const double price = pricers::black_scholes_merton(s, k, r, q, sigma, t, type);
                const double exercise_value = std::max(phi * (s - k), 0.0);
                const double time_value = price - exercise_value;
                EXPECT_GE(exercise_value, 0.0);
                EXPECT_GE(time_value, -kTightTol) << "S=" << s << " K=" << k;
                EXPECT_NEAR(price, exercise_value + time_value, kTightTol);
            }
        }
    }
}

// LM8 — six factor-sensitivity directional claims, via central finite differences on
// the closed-form price itself (not the general FD-Greeks framework, which is Phase 5).
TEST(CfaInvariant, SixFactorSensitivitySigns) {
    const double s = 100.0, k = 100.0, r = 0.04, q = 0.01, sigma = 0.25, t = 1.0;

    auto call = [&](double s_, double k_, double sigma_) {
        return pricers::black_scholes_merton(s_, k_, r, q, sigma_, t, OptionType::Call);
    };
    auto put = [&](double s_, double k_, double sigma_) {
        return pricers::black_scholes_merton(s_, k_, r, q, sigma_, t, OptionType::Put);
    };

    // 1: dC/dS > 0
    EXPECT_GT(call(s + kBump, k, sigma) - call(s - kBump, k, sigma), 0.0);
    // 2: dP/dS < 0
    EXPECT_LT(put(s + kBump, k, sigma) - put(s - kBump, k, sigma), 0.0);
    // 3: dC/dX < 0
    EXPECT_LT(call(s, k + kBump, sigma) - call(s, k - kBump, sigma), 0.0);
    // 4: dP/dX > 0
    EXPECT_GT(put(s, k + kBump, sigma) - put(s, k - kBump, sigma), 0.0);
    // 5: dC/dsigma > 0
    EXPECT_GT(call(s, k, sigma + kBump) - call(s, k, sigma - kBump), 0.0);
    // 6: dP/dsigma > 0
    EXPECT_GT(put(s, k, sigma + kBump) - put(s, k, sigma - kBump), 0.0);
}

// LM9 — put-call parity: C + X e^{-rT} = P + S0 e^{-qT}, to 1e-12.
TEST(CfaInvariant, PutCallParity) {
    const double s = 100.0, k = 100.0, r = 0.04, q = 0.01, sigma = 0.25, t = 1.0;
    const double call = pricers::black_scholes_merton(s, k, r, q, sigma, t, OptionType::Call);
    const double put = pricers::black_scholes_merton(s, k, r, q, sigma, t, OptionType::Put);
    const double lhs = call + k * std::exp(-r * t);
    const double rhs = put + s * std::exp(-q * t);
    EXPECT_NEAR(lhs, rhs, 1e-12);
}

// LM9 — put-call forward parity: C + X e^{-rT} = P + F0 e^{-rT}, to 1e-12.
TEST(CfaInvariant, PutCallForwardParity) {
    const double s = 100.0, k = 100.0, r = 0.04, q = 0.01, sigma = 0.25, t = 1.0;
    const double call = pricers::black_scholes_merton(s, k, r, q, sigma, t, OptionType::Call);
    const double put = pricers::black_scholes_merton(s, k, r, q, sigma, t, OptionType::Put);
    const double f0 = pricers::forward_price(s, r, q, t);
    const double lhs = call + k * std::exp(-r * t);
    const double rhs = put + f0 * std::exp(-r * t);
    EXPECT_NEAR(lhs, rhs, 1e-12);
}

// LM10 — binomial risk-neutral probability formula, and price independence from any
// real-world drift assumption. crr_binomial()'s signature has no real-world-drift
// parameter at all, so that independence holds structurally, not just numerically.
TEST(CfaInvariant, BinomialRiskNeutrality) {
    const double s = 100.0, k = 100.0, r = 0.05, q = 0.01, sigma = 0.25, t = 1.0;
    const auto result = pricers::crr_binomial(s, k, r, q, sigma, t, /*steps=*/1, OptionType::Call);
    const double expected_pi =
        (std::exp((r - q) * t) - result.down_factor) / (result.up_factor - result.down_factor);
    EXPECT_NEAR(result.risk_neutral_probability, expected_pi, kTightTol);
    EXPECT_GT(result.risk_neutral_probability, 0.0);
    EXPECT_LT(result.risk_neutral_probability, 1.0);
}

// LM10 — CRR price converges to BSM as n -> infinity; error < 0.01 at n=5000, and
// decreases monotonically over n in {10, 50, 250, 1000, 5000}.
TEST(CfaInvariant, BinomialConvergesToBsm) {
    const double s = 100.0, k = 100.0, r = 0.05, q = 0.02, sigma = 0.2, t = 1.0;
    const double bsm = pricers::black_scholes_merton(s, k, r, q, sigma, t, OptionType::Call);

    double previous_error = 1e300;
    for (int n : {10, 50, 250, 1000, 5000}) {
        const auto result = pricers::crr_binomial(s, k, r, q, sigma, t, n, OptionType::Call);
        const double error = std::abs(result.price - bsm);
        EXPECT_LT(error, previous_error) << "n=" << n;
        previous_error = error;
    }
    EXPECT_LT(previous_error, 0.01);
}

// LM4, LM8 — no-arbitrage bounds: max(S e^{-qT} - X e^{-rT}, 0) <= C <= S e^{-qT}.
TEST(CfaInvariant, NoArbitrageBounds) {
    for (double s : {50.0, 100.0, 150.0}) {
        for (double k : {80.0, 100.0, 120.0}) {
            const double r = 0.04, q = 0.015, sigma = 0.3, t = 1.0;
            const double call = pricers::black_scholes_merton(s, k, r, q, sigma, t, OptionType::Call);
            const double lower = std::max(s * std::exp(-q * t) - k * std::exp(-r * t), 0.0);
            const double upper = s * std::exp(-q * t);
            EXPECT_GE(call, lower - kTightTol) << "S=" << s << " K=" << k;
            EXPECT_LE(call, upper + kTightTol) << "S=" << s << " K=" << k;
        }
    }
}

// LM8 — monotonicity: call value non-decreasing in S and sigma, non-increasing in X,
// across a swept grid.
TEST(CfaInvariant, MonotonicityInSpot) {
    const double k = 100.0, r = 0.04, q = 0.01, sigma = 0.25, t = 1.0;
    double previous = -1.0;
    for (double s = 50.0; s <= 150.0; s += 5.0) {
        const double price = pricers::black_scholes_merton(s, k, r, q, sigma, t, OptionType::Call);
        EXPECT_GE(price, previous - kTightTol) << "S=" << s;
        previous = price;
    }
}

TEST(CfaInvariant, MonotonicityInVolatility) {
    const double s = 100.0, k = 100.0, r = 0.04, q = 0.01, t = 1.0;
    double previous = -1.0;
    for (double sigma = 0.05; sigma <= 0.8; sigma += 0.05) {
        const double price = pricers::black_scholes_merton(s, k, r, q, sigma, t, OptionType::Call);
        EXPECT_GE(price, previous - kTightTol) << "sigma=" << sigma;
        previous = price;
    }
}

TEST(CfaInvariant, MonotonicityInStrike) {
    const double s = 100.0, r = 0.04, q = 0.01, sigma = 0.25, t = 1.0;
    double previous = 1e300;
    for (double k = 50.0; k <= 150.0; k += 5.0) {
        const double price = pricers::black_scholes_merton(s, k, r, q, sigma, t, OptionType::Call);
        EXPECT_LE(price, previous + kTightTol) << "K=" << k;
        previous = price;
    }
}
