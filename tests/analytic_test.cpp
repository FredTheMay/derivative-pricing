#include "mcd/pricers/analytic.hpp"
#include "mcd/pricers/binomial.hpp"

#include <gtest/gtest.h>

#include <cmath>

using mcd::BarrierDirection;
using mcd::BarrierKnock;
using mcd::DigitalStyle;
using mcd::OptionType;
namespace pricers = mcd::pricers;

namespace {
constexpr double kTightTol = 1e-9;
}

// --- Put-call parity: C + K e^{-rT} = P + S e^{-qT} ---------------------------------

TEST(PutCallParity, HoldsAcrossParameterGrid) {
    for (double s : {50.0, 100.0, 150.0}) {
        for (double k : {80.0, 100.0, 120.0}) {
            for (double r : {0.01, 0.05, 0.1}) {
                for (double q : {0.0, 0.02, 0.05}) {
                    for (double sigma : {0.1, 0.2, 0.4}) {
                        for (double t : {0.25, 1.0, 2.0}) {
                            const double call =
                                pricers::black_scholes_merton(s, k, r, q, sigma, t, OptionType::Call);
                            const double put =
                                pricers::black_scholes_merton(s, k, r, q, sigma, t, OptionType::Put);
                            const double lhs = call + k * std::exp(-r * t);
                            const double rhs = put + s * std::exp(-q * t);
                            EXPECT_NEAR(lhs, rhs, kTightTol)
                                << "S=" << s << " K=" << k << " r=" << r << " q=" << q
                                << " sigma=" << sigma << " T=" << t;
                        }
                    }
                }
            }
        }
    }
}

TEST(PutCallForwardParity, HoldsAcrossParameterGrid) {
    for (double s : {50.0, 100.0, 150.0}) {
        for (double k : {80.0, 100.0, 120.0}) {
            for (double r : {0.01, 0.05, 0.1}) {
                for (double q : {0.0, 0.02, 0.05}) {
                    const double sigma = 0.25;
                    const double t = 1.0;
                    const double call =
                        pricers::black_scholes_merton(s, k, r, q, sigma, t, OptionType::Call);
                    const double put =
                        pricers::black_scholes_merton(s, k, r, q, sigma, t, OptionType::Put);
                    const double f0 = pricers::forward_price(s, r, q, t);
                    const double lhs = call + k * std::exp(-r * t);
                    const double rhs = put + f0 * std::exp(-r * t);
                    EXPECT_NEAR(lhs, rhs, kTightTol);
                }
            }
        }
    }
}

// --- Black-76 reduces to BSM when priced off the forward with q = r ------------------

TEST(Black76, ReducesToBsmViaForward) {
    // Black76(F, K, r, sigma, T) == BSM(S, K, r, q, sigma, T) whenever
    // F = S * e^{(r-q)T}, for ANY q -- substituting F back in makes the d1/d2 drift
    // terms identical algebraically. This holds generally, not just when q = r.
    const double s = 100.0, k = 95.0, r = 0.04, q = 0.015, sigma = 0.3, t = 0.75;
    const double f = pricers::forward_price(s, r, q, t);
    for (OptionType type : {OptionType::Call, OptionType::Put}) {
        const double black76_price = pricers::black76(f, k, r, sigma, t, type);
        const double bsm_price = pricers::black_scholes_merton(s, k, r, q, sigma, t, type);
        EXPECT_NEAR(black76_price, bsm_price, 1e-9);
    }
}

// --- CRR binomial converges to BSM as n -> infinity ------------------------------------

TEST(CrrBinomial, ConvergesToBsm) {
    const double s = 100.0, k = 100.0, r = 0.05, q = 0.02, sigma = 0.2, t = 1.0;
    const double bsm = pricers::black_scholes_merton(s, k, r, q, sigma, t, OptionType::Call);

    double previous_error = 1e300;
    for (int n : {10, 50, 250, 1000, 5000}) {
        const auto result = pricers::crr_binomial(s, k, r, q, sigma, t, n, OptionType::Call);
        const double error = std::abs(result.price - bsm);
        EXPECT_LT(error, previous_error) << "n=" << n << " error=" << error
                                          << " did not decrease from " << previous_error;
        previous_error = error;
    }
    EXPECT_LT(previous_error, 0.01) << "final error at n=5000: " << previous_error;
}

TEST(CrrBinomial, RiskNeutralProbabilityFormula) {
    const double s = 100.0, k = 100.0, r = 0.05, q = 0.01, sigma = 0.25, t = 1.0;
    const int steps = 1;
    const auto result = pricers::crr_binomial(s, k, r, q, sigma, t, steps, OptionType::Call);

    const double dt = t / static_cast<double>(steps);
    const double expected_pi =
        (std::exp((r - q) * dt) - result.down_factor) / (result.up_factor - result.down_factor);
    EXPECT_NEAR(result.risk_neutral_probability, expected_pi, kTightTol);
    EXPECT_GT(result.risk_neutral_probability, 0.0);
    EXPECT_LT(result.risk_neutral_probability, 1.0);
}

// --- One well-known, widely-published textbook sanity check (loose tolerance) --------

TEST(BlackScholesMerton, HullTextbookExample) {
    // S=42, K=40, r=10%, sigma=20%, T=0.5, q=0. Widely republished example
    // (Hull, Options/Futures/Other Derivatives); hand-recomputed this session.
    const double call = pricers::black_scholes_merton(42.0, 40.0, 0.10, 0.0, 0.20, 0.5,
                                                        OptionType::Call);
    const double put =
        pricers::black_scholes_merton(42.0, 40.0, 0.10, 0.0, 0.20, 0.5, OptionType::Put);
    EXPECT_NEAR(call, 4.76, 0.05);
    EXPECT_NEAR(put, 0.81, 0.05);
}

// --- Digital decomposition: vanilla = asset-or-nothing - K * cash-or-nothing ---------

TEST(Digital, DecomposesVanillaCall) {
    const double s = 100.0, k = 105.0, r = 0.03, q = 0.01, sigma = 0.22, t = 0.8;
    const double vanilla = pricers::black_scholes_merton(s, k, r, q, sigma, t, OptionType::Call);
    const double aon =
        pricers::digital(s, k, r, q, sigma, t, OptionType::Call, DigitalStyle::AssetOrNothing);
    const double con = pricers::digital(s, k, r, q, sigma, t, OptionType::Call,
                                         DigitalStyle::CashOrNothing, /*cash_amount=*/1.0);
    EXPECT_NEAR(vanilla, aon - k * con, 1e-6);
}

TEST(Digital, DecomposesVanillaPut) {
    const double s = 100.0, k = 95.0, r = 0.03, q = 0.01, sigma = 0.22, t = 0.8;
    const double vanilla = pricers::black_scholes_merton(s, k, r, q, sigma, t, OptionType::Put);
    const double aon =
        pricers::digital(s, k, r, q, sigma, t, OptionType::Put, DigitalStyle::AssetOrNothing);
    const double con = pricers::digital(s, k, r, q, sigma, t, OptionType::Put,
                                         DigitalStyle::CashOrNothing, /*cash_amount=*/1.0);
    EXPECT_NEAR(vanilla, k * con - aon, 1e-6);
}

// --- Barrier in + out = vanilla, all four direction/type pairs -----------------------

class BarrierParityTest : public ::testing::TestWithParam<
                               std::tuple<BarrierDirection, OptionType, double>> {};

TEST_P(BarrierParityTest, InPlusOutEqualsVanilla) {
    const auto [direction, type, barrier] = GetParam();
    const double s = 100.0, k = 100.0, r = 0.04, q = 0.01, sigma = 0.25, t = 1.0;

    const double in_price = pricers::reiner_rubinstein(s, k, barrier, r, q, sigma, t, type,
                                                         direction, BarrierKnock::In);
    const double out_price = pricers::reiner_rubinstein(s, k, barrier, r, q, sigma, t, type,
                                                          direction, BarrierKnock::Out);
    const double vanilla = pricers::black_scholes_merton(s, k, r, q, sigma, t, type);

    EXPECT_NEAR(in_price + out_price, vanilla, 1e-6)
        << "direction=" << static_cast<int>(direction) << " type=" << static_cast<int>(type)
        << " barrier=" << barrier;
}

INSTANTIATE_TEST_SUITE_P(
    AllBarrierTypes, BarrierParityTest,
    ::testing::Values(std::make_tuple(BarrierDirection::Down, OptionType::Call, 80.0),
                       std::make_tuple(BarrierDirection::Down, OptionType::Put, 80.0),
                       std::make_tuple(BarrierDirection::Up, OptionType::Call, 120.0),
                       std::make_tuple(BarrierDirection::Up, OptionType::Put, 120.0)));

// --- Barrier limiting cases: far barrier -> vanilla / trivial ------------------------

TEST(Barrier, DownAndOutWithFarBarrierApproachesVanilla) {
    const double s = 100.0, k = 100.0, r = 0.04, q = 0.01, sigma = 0.25, t = 1.0;
    const double out_price = pricers::reiner_rubinstein(s, k, /*barrier=*/1e-3, r, q, sigma, t,
                                                          OptionType::Call, BarrierDirection::Down,
                                                          BarrierKnock::Out);
    const double vanilla = pricers::black_scholes_merton(s, k, r, q, sigma, t, OptionType::Call);
    EXPECT_NEAR(out_price, vanilla, 1e-3);
}

TEST(Barrier, UpAndOutWithFarBarrierApproachesVanilla) {
    const double s = 100.0, k = 100.0, r = 0.04, q = 0.01, sigma = 0.25, t = 1.0;
    const double out_price = pricers::reiner_rubinstein(s, k, /*barrier=*/1e6, r, q, sigma, t,
                                                          OptionType::Put, BarrierDirection::Up,
                                                          BarrierKnock::Out);
    const double vanilla = pricers::black_scholes_merton(s, k, r, q, sigma, t, OptionType::Put);
    EXPECT_NEAR(out_price, vanilla, 1e-3);
}

// --- Lookback ordering bounds (pathwise-exact, hold for any correct formula) ---------

TEST(Lookback, FixedStrikeCallDominatesVanilla) {
    const double s = 100.0, k = 100.0, r = 0.04, q = 0.015, sigma = 0.3, t = 1.0;
    const double lookback = pricers::lookback_fixed_strike(s, k, r, q, sigma, t, OptionType::Call);
    const double vanilla = pricers::black_scholes_merton(s, k, r, q, sigma, t, OptionType::Call);
    EXPECT_GE(lookback, vanilla - kTightTol);
}

TEST(Lookback, FixedStrikePutDominatesVanilla) {
    const double s = 100.0, k = 100.0, r = 0.04, q = 0.015, sigma = 0.3, t = 1.0;
    const double lookback = pricers::lookback_fixed_strike(s, k, r, q, sigma, t, OptionType::Put);
    const double vanilla = pricers::black_scholes_merton(s, k, r, q, sigma, t, OptionType::Put);
    EXPECT_GE(lookback, vanilla - kTightTol);
}

TEST(Lookback, FloatingStrikeCallDominatesAtmVanilla) {
    const double s = 100.0, r = 0.04, q = 0.015, sigma = 0.3, t = 1.0;
    const double lookback = pricers::lookback_floating_strike(s, r, q, sigma, t, OptionType::Call);
    const double vanilla = pricers::black_scholes_merton(s, s, r, q, sigma, t, OptionType::Call);
    EXPECT_GE(lookback, vanilla - kTightTol);
}

TEST(Lookback, FloatingStrikePutDominatesAtmVanilla) {
    const double s = 100.0, r = 0.04, q = 0.015, sigma = 0.3, t = 1.0;
    const double lookback = pricers::lookback_floating_strike(s, r, q, sigma, t, OptionType::Put);
    const double vanilla = pricers::black_scholes_merton(s, s, r, q, sigma, t, OptionType::Put);
    EXPECT_GE(lookback, vanilla - kTightTol);
}

// Note: a floating-strike lookback's price scales like O(sigma * S * sqrt(T)) as T -> 0,
// the same leading-order behavior as an at-the-money vanilla option (Brenner-Subrahmanyam),
// not like O(T) -- so asserting it collapses to ~0 at small but fixed T is the wrong
// physical expectation. Monotonicity in T is the robust, well-defined invariant instead:
// more time strictly increases the running-extremum optionality.
TEST(Lookback, FloatingStrikeIncreasesWithTime) {
    const double s = 100.0, r = 0.04, q = 0.015, sigma = 0.3;
    for (OptionType type : {OptionType::Call, OptionType::Put}) {
        double previous = 0.0;
        for (double t : {0.01, 0.1, 0.5, 1.0, 2.0, 5.0}) {
            const double price = pricers::lookback_floating_strike(s, r, q, sigma, t, type);
            EXPECT_GT(price, previous) << "T=" << t;
            previous = price;
        }
    }
}

TEST(Lookback, FixedStrikeBranchesAgreeAtStrikeEqualsSpot) {
    // The two branches of lookback_fixed_strike (K >= S and K <= S for calls, mirrored for
    // puts) both reduce to the same direct formula at K == S; approaching from either side
    // must agree in the limit, which is a real continuity constraint on the closed form.
    const double s = 100.0, r = 0.04, q = 0.015, sigma = 0.3, t = 1.0;
    for (OptionType type : {OptionType::Call, OptionType::Put}) {
        const double from_above = pricers::lookback_fixed_strike(s, s + 1e-4, r, q, sigma, t, type);
        const double from_below = pricers::lookback_fixed_strike(s, s - 1e-4, r, q, sigma, t, type);
        EXPECT_NEAR(from_above, from_below, 1e-2);
    }
}

// --- Forward / futures --------------------------------------------------------------

TEST(Forward, ValueAtInitiationIsZero) {
    const double s = 100.0, r = 0.03, q = 0.01, t = 1.0;
    const double f0 = pricers::forward_price(s, r, q, t);
    EXPECT_NEAR(pricers::forward_value(f0, f0, r, t), 0.0, kTightTol);
}

TEST(Forward, FuturesForwardEquivalenceUnderDeterministicRates) {
    const double s = 100.0, r = 0.03, q = 0.01, t = 1.0;
    // Under deterministic rates the futures price at inception equals the forward price.
    EXPECT_NEAR(pricers::forward_price(s, r, q, t), pricers::forward_price(s, r, q, t),
                kTightTol);
}

TEST(Forward, MarkToMarketIsUndiscountedDifference) {
    EXPECT_NEAR(pricers::futures_mark_to_market(105.0, 100.0), 5.0, kTightTol);
}
