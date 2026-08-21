#include "mcd/core/rng.hpp"
#include "mcd/greeks/finite_difference.hpp"
#include "mcd/greeks/likelihood_ratio.hpp"
#include "mcd/pricers/analytic.hpp"
#include "mcd/pricers/monte_carlo.hpp"

#include <gtest/gtest.h>

#include <bit>
#include <cmath>
#include <cstdio>

using mcd::OptionType;
namespace pricers = mcd::pricers;
namespace greeks = mcd::greeks;

namespace {

// Same tight-deterministic-FD-on-the-closed-form oracle as tests/greeks_test.cpp -- kept
// as its own small copy per this project's precedent (each test file owns its helpers),
// not shared, since it's a handful of lines.
struct AnalyticGreeks {
    double delta, gamma, vega, theta, rho;
};

AnalyticGreeks analytic_greeks(double spot, double strike, double rate, double carry_yield,
                                double vol, double time, OptionType type) {
    const double hs = spot * 1e-5;
    const double hv = vol * 1e-5;
    const double hr = 0.01 * 1e-3;
    const double ht = time * 1e-5;
    const auto bsm = [&](double s, double r, double q, double v, double t) {
        return pricers::black_scholes_merton(s, strike, r, q, v, t, type);
    };
    const double v0 = bsm(spot, rate, carry_yield, vol, time);
    return AnalyticGreeks{
        .delta = (bsm(spot + hs, rate, carry_yield, vol, time) -
                  bsm(spot - hs, rate, carry_yield, vol, time)) /
                 (2.0 * hs),
        .gamma = (bsm(spot + hs, rate, carry_yield, vol, time) - 2.0 * v0 +
                  bsm(spot - hs, rate, carry_yield, vol, time)) /
                 (hs * hs),
        .vega = (bsm(spot, rate, carry_yield, vol + hv, time) -
                 bsm(spot, rate, carry_yield, vol - hv, time)) /
                (2.0 * hv),
        .theta = -(bsm(spot, rate, carry_yield, vol, time + ht) -
                   bsm(spot, rate, carry_yield, vol, time - ht)) /
                 (2.0 * ht),
        .rho = (bsm(spot, rate + hr, carry_yield, vol, time) -
                bsm(spot, rate - hr, carry_yield, vol, time)) /
               (2.0 * hr),
    };
}

struct GreeksCase {
    double s, k, r, q, sigma, t;
    OptionType type;
};

} // namespace

// --- LR Greeks vs. BSM analytic, for European -- the sanity check that the re-derived
// score functions are actually correct, using the one product with an independent oracle.

class LrGreeksEuropeanVsAnalytic : public ::testing::TestWithParam<GreeksCase> {};

TEST_P(LrGreeksEuropeanVsAnalytic, WithinThreeStandardErrors) {
    const auto c = GetParam();
    const std::uint64_t path_count = 500'000;
    const std::uint64_t seed = 4242;

    const auto lr = greeks::likelihood_ratio_european(c.s, c.k, c.r, c.q, c.sigma, c.t, c.type,
                                                        path_count, seed);
    const auto analytic = analytic_greeks(c.s, c.k, c.r, c.q, c.sigma, c.t, c.type);

    auto check = [](const char* name, const greeks::LrGreeksResult& lr_val, double analytic_val) {
        const double deviation = std::abs(lr_val.value - analytic_val);
        EXPECT_LT(deviation, 3.0 * lr_val.standard_error)
            << name << ": lr=" << lr_val.value << " analytic=" << analytic_val
            << " deviation=" << deviation << " SE=" << lr_val.standard_error
            << " (deviation/SE=" << deviation / lr_val.standard_error << ")";
    };
    check("delta", lr.delta, analytic.delta);
    check("gamma", lr.gamma, analytic.gamma);
    check("vega", lr.vega, analytic.vega);
    ASSERT_TRUE(lr.theta.has_value());
    check("theta", *lr.theta, analytic.theta);
    check("rho", lr.rho, analytic.rho);
}

INSTANTIATE_TEST_SUITE_P(
    ParameterMatrix, LrGreeksEuropeanVsAnalytic,
    ::testing::Values(GreeksCase{100, 100, 0.05, 0.00, 0.20, 1.0, OptionType::Call},
                       GreeksCase{100, 100, 0.05, 0.00, 0.20, 1.0, OptionType::Put},
                       GreeksCase{100, 80, 0.05, 0.02, 0.25, 0.5, OptionType::Call},
                       GreeksCase{100, 120, 0.05, 0.02, 0.30, 2.0, OptionType::Put}));

// --- LR Greeks vs. BSM analytic, for digital -- no FD oracle can be trusted here (that's
// the whole point), so this validates against the one product where an independent
// closed-form digital price/Greek family exists, at parameters away from the
// discontinuity, where FD is still reasonably trustworthy for cross-checking LR itself.

TEST(LrGreeksDigital, DeltaWithinThreeStandardErrorsAwayFromStrike) {
    // Digital delta has a clean closed form derivable from the analytic digital price;
    // cross-check LR against a tight FD bump on the *analytic* (deterministic) digital
    // pricer instead of re-deriving a new closed form by hand.
    const double s = 90.0, k = 100.0, r = 0.05, q = 0.0, sigma = 0.25, t = 1.0;
    const double h = s * 1e-5;
    const auto digital_price = [&](double spot) {
        return pricers::digital(spot, k, r, q, sigma, t, OptionType::Call,
                                 mcd::DigitalStyle::CashOrNothing, 1.0);
    };
    const double analytic_delta = (digital_price(s + h) - digital_price(s - h)) / (2.0 * h);

    const auto lr = greeks::likelihood_ratio_digital(s, k, r, q, sigma, t, OptionType::Call,
                                                       mcd::DigitalStyle::CashOrNothing, 1.0,
                                                       500'000, 99);
    const double deviation = std::abs(lr.delta.value - analytic_delta);
    EXPECT_LT(deviation, 3.0 * lr.delta.standard_error)
        << "lr=" << lr.delta.value << " analytic=" << analytic_delta
        << " SE=" << lr.delta.standard_error;
}

// --- The actual point of this stretch goal: LR vs. FD gamma standard error near a
// discontinuity, quantified -- not just asserted. CLAUDE.md sec.7 item 1 names this
// exact case ("fixes gamma for digitals and barriers").

TEST(LrVsFdGamma, LrHasLowerStandardErrorNearDigitalStrike) {
    // At the money -- exactly where the digital's discontinuity lives, and exactly where
    // FD gamma (dividing by h^2 across a step function) is at its worst.
    const double s = 100.0, k = 100.0, r = 0.05, q = 0.0, sigma = 0.25, t = 1.0;
    const std::uint64_t path_count = 200'000;
    const std::uint64_t seed = 7;

    const auto lr = greeks::likelihood_ratio_digital(s, k, r, q, sigma, t, OptionType::Call,
                                                       mcd::DigitalStyle::CashOrNothing, 1.0,
                                                       path_count, seed);

    const auto bumps = greeks::default_bump_sizes(s, sigma, t);
    // finite_difference_european is written for European payoffs; reuse its bump size
    // directly against the digital pricer for a fair FD-gamma comparison at matched
    // (path_count, seed, bump size).
    const auto v_plus = pricers::monte_carlo_digital(s + bumps.spot, k, r, q, sigma, t,
                                                       OptionType::Call,
                                                       mcd::DigitalStyle::CashOrNothing, 1.0,
                                                       path_count, seed);
    const auto v0 = pricers::monte_carlo_digital(s, k, r, q, sigma, t, OptionType::Call,
                                                  mcd::DigitalStyle::CashOrNothing, 1.0,
                                                  path_count, seed);
    const auto v_minus = pricers::monte_carlo_digital(s - bumps.spot, k, r, q, sigma, t,
                                                        OptionType::Call,
                                                        mcd::DigitalStyle::CashOrNothing, 1.0,
                                                        path_count, seed);
    const double fd_gamma =
        (v_plus.price - 2.0 * v0.price + v_minus.price) / (bumps.spot * bumps.spot);
    // Conservative (independence-assumed) SE bound for the FD gamma estimator, same
    // technique as tests/greeks_test.cpp's conservative_se_bound.
    const double fd_gamma_se =
        std::sqrt(v_plus.standard_error * v_plus.standard_error +
                  4.0 * v0.standard_error * v0.standard_error +
                  v_minus.standard_error * v_minus.standard_error) /
        (bumps.spot * bumps.spot);

    std::printf("[LR vs FD] digital gamma at S=K: LR=%.6f (SE=%.6f)  FD=%.6f (SE=%.6f)  "
                "SE ratio (FD/LR) = %.2fx\n",
                lr.gamma.value, lr.gamma.standard_error, fd_gamma, fd_gamma_se,
                fd_gamma_se / lr.gamma.standard_error);

    EXPECT_LT(lr.gamma.standard_error, fd_gamma_se)
        << "LR SE=" << lr.gamma.standard_error << " FD SE=" << fd_gamma_se;
}

TEST(LrVsFdGamma, LrHasLowerStandardErrorNearBarrierLevel) {
    const double s = 100.0, k = 100.0, barrier = 105.0, r = 0.05, q = 0.0, sigma = 0.25,
                 t = 0.5;
    const int monitoring_points = 50;
    const std::uint64_t path_count = 100'000;
    const std::uint64_t seed = 11;

    const auto lr = greeks::likelihood_ratio_barrier(
        s, k, barrier, r, q, sigma, t, OptionType::Call, mcd::BarrierDirection::Up,
        mcd::BarrierKnock::Out, 0.0, monitoring_points, path_count, seed);

    const auto bumps = greeks::default_bump_sizes(s, sigma, t);
    const auto v_plus = pricers::monte_carlo_barrier(
        s + bumps.spot, k, barrier, r, q, sigma, t, OptionType::Call,
        mcd::BarrierDirection::Up, mcd::BarrierKnock::Out, 0.0, monitoring_points, path_count,
        seed);
    const auto v0 = pricers::monte_carlo_barrier(
        s, k, barrier, r, q, sigma, t, OptionType::Call, mcd::BarrierDirection::Up,
        mcd::BarrierKnock::Out, 0.0, monitoring_points, path_count, seed);
    const auto v_minus = pricers::monte_carlo_barrier(
        s - bumps.spot, k, barrier, r, q, sigma, t, OptionType::Call,
        mcd::BarrierDirection::Up, mcd::BarrierKnock::Out, 0.0, monitoring_points, path_count,
        seed);
    const double fd_gamma =
        (v_plus.price - 2.0 * v0.price + v_minus.price) / (bumps.spot * bumps.spot);
    const double fd_gamma_se =
        std::sqrt(v_plus.standard_error * v_plus.standard_error +
                  4.0 * v0.standard_error * v0.standard_error +
                  v_minus.standard_error * v_minus.standard_error) /
        (bumps.spot * bumps.spot);

    std::printf("[LR vs FD] barrier gamma near H=105: LR=%.6f (SE=%.6f)  FD=%.6f (SE=%.6f)  "
                "SE ratio (FD/LR) = %.2fx\n",
                lr.gamma.value, lr.gamma.standard_error, fd_gamma, fd_gamma_se,
                fd_gamma_se / lr.gamma.standard_error);

    EXPECT_LT(lr.gamma.standard_error, fd_gamma_se)
        << "LR SE=" << lr.gamma.standard_error << " FD SE=" << fd_gamma_se;
}

// --- Determinism: identical seed => bitwise-identical LR Greeks (CLAUDE.md's hard
// determinism guarantee applies to every pricer in this project, including this one).

TEST(LrGreeksDeterminism, IdenticalSeedProducesBitwiseIdenticalResult) {
    const auto run = [] {
        return greeks::likelihood_ratio_european(100, 100, 0.05, 0.0, 0.2, 1.0,
                                                   OptionType::Call, 50'000, 123);
    };
    const auto a = run();
    const auto b = run();
    EXPECT_EQ(std::bit_cast<std::uint64_t>(a.delta.value), std::bit_cast<std::uint64_t>(b.delta.value));
    EXPECT_EQ(std::bit_cast<std::uint64_t>(a.gamma.value), std::bit_cast<std::uint64_t>(b.gamma.value));
    EXPECT_EQ(std::bit_cast<std::uint64_t>(a.vega.value), std::bit_cast<std::uint64_t>(b.vega.value));
}
