#include "mcd/greeks/finite_difference.hpp"
#include "mcd/greeks/likelihood_ratio.hpp"
#include "mcd/greeks/pathwise.hpp"
#include "mcd/pricers/analytic.hpp"
#include "mcd/pricers/monte_carlo.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <cstdio>

using mcd::OptionType;
namespace pricers = mcd::pricers;
namespace greeks = mcd::greeks;

namespace {

// Same tight-deterministic-FD-on-the-closed-form oracle used in tests/greeks_test.cpp
// and tests/likelihood_ratio_test.cpp -- own small copy per this project's precedent.
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

// --- Pathwise Greeks vs. BSM analytic, for European -- the sanity check that the
// pathwise derivation is correct.

class PathwiseGreeksEuropeanVsAnalytic : public ::testing::TestWithParam<GreeksCase> {};

TEST_P(PathwiseGreeksEuropeanVsAnalytic, WithinThreeStandardErrors) {
    const auto c = GetParam();
    const std::uint64_t path_count = 500'000;
    const std::uint64_t seed = 606;

    const auto pw = greeks::pathwise_european(c.s, c.k, c.r, c.q, c.sigma, c.t, c.type,
                                               path_count, seed);
    const auto analytic = analytic_greeks(c.s, c.k, c.r, c.q, c.sigma, c.t, c.type);

    auto check = [](const char* name, const greeks::PathwiseGreeksResult& pw_val,
                     double analytic_val) {
        const double deviation = std::abs(pw_val.value - analytic_val);
        EXPECT_LT(deviation, 3.0 * pw_val.standard_error)
            << name << ": pathwise=" << pw_val.value << " analytic=" << analytic_val
            << " deviation=" << deviation << " SE=" << pw_val.standard_error;
    };
    check("delta", pw.delta, analytic.delta);
    check("vega", pw.vega, analytic.vega);
    check("rho", pw.rho, analytic.rho);
}

INSTANTIATE_TEST_SUITE_P(
    ParameterMatrix, PathwiseGreeksEuropeanVsAnalytic,
    ::testing::Values(GreeksCase{100, 100, 0.05, 0.00, 0.20, 1.0, OptionType::Call},
                       GreeksCase{100, 100, 0.05, 0.00, 0.20, 1.0, OptionType::Put},
                       GreeksCase{100, 80, 0.05, 0.02, 0.25, 0.5, OptionType::Call},
                       GreeksCase{100, 120, 0.05, 0.02, 0.30, 2.0, OptionType::Put}));

// --- Pathwise Greeks vs. FD, for Asian -- cross-checked against Phase 5's FD machinery
// applied directly to the Asian pricer, since there's no closed-form Asian Greek to check
// against (arithmetic Asian has no closed form at all; this is exactly why every product
// in this project needs *an* independent oracle, not necessarily the same one).

TEST(PathwiseGreeksAsian, DeltaWithinThreeStandardErrorsOfFiniteDifference) {
    const double s = 100.0, k = 100.0, r = 0.05, q = 0.0, sigma = 0.25, t = 1.0;
    const int monitoring_points = 12;
    const std::uint64_t path_count = 300'000;
    const std::uint64_t seed = 303;

    const auto pw = greeks::pathwise_asian(s, k, r, q, sigma, t, OptionType::Call,
                                            mcd::StrikeStyle::Fixed, mcd::AverageStyle::Arithmetic,
                                            monitoring_points, path_count, seed);

    const double h = s * 1e-3;
    const auto price = [&](double spot) {
        return pricers::monte_carlo_asian(spot, k, r, q, sigma, t, OptionType::Call,
                                           mcd::StrikeStyle::Fixed, mcd::AverageStyle::Arithmetic,
                                           monitoring_points, path_count, seed);
    };
    const auto v_plus = price(s + h);
    const auto v_minus = price(s - h);
    const double fd_delta = (v_plus.price - v_minus.price) / (2.0 * h);
    const double fd_delta_se =
        std::sqrt(v_plus.standard_error * v_plus.standard_error +
                  v_minus.standard_error * v_minus.standard_error) /
        (2.0 * h);

    const double deviation = std::abs(pw.delta.value - fd_delta);
    const double combined_se = std::sqrt(pw.delta.standard_error * pw.delta.standard_error +
                                          fd_delta_se * fd_delta_se);
    EXPECT_LT(deviation, 3.0 * combined_se)
        << "pathwise=" << pw.delta.value << " (SE=" << pw.delta.standard_error << ") fd="
        << fd_delta << " (SE=" << fd_delta_se << ")";
}

// --- The actual point of this stretch goal: pathwise's failure mode, measured, not just
// asserted. CLAUDE.md sec.7 item 2 explicitly asks for "where each breaks."

TEST(PathwiseFailureMode, NaiveDigitalDeltaConvergesToZeroNotTheTrueValue) {
    const double s = 100.0, k = 100.0, r = 0.05, q = 0.0, sigma = 0.25, t = 1.0;
    const double cash_amount = 1.0;

    const double true_delta_h = s * 1e-5;
    const auto digital_price = [&](double spot) {
        return pricers::digital(spot, k, r, q, sigma, t, OptionType::Call,
                                 mcd::DigitalStyle::CashOrNothing, cash_amount);
    };
    const double true_delta =
        (digital_price(s + true_delta_h) - digital_price(s - true_delta_h)) /
        (2.0 * true_delta_h);

    const auto pw = greeks::pathwise_digital_delta_naive_and_broken(
        s, k, r, q, sigma, t, OptionType::Call, cash_amount, 200'000, 505);

    std::printf("[Pathwise failure mode] naive pathwise digital delta = %.10f (SE=%.10f), "
                "true delta = %.6f\n",
                pw.value, pw.standard_error, true_delta);

    // Not just "different" -- exactly zero, and the true value is nowhere near it.
    EXPECT_EQ(pw.value, 0.0);
    EXPECT_GT(std::abs(true_delta), 0.005)
        << "true delta should be clearly nonzero for this to be a meaningful failure demo";
}

// --- Determinism: identical seed => bitwise-identical pathwise Greeks.

TEST(PathwiseGreeksDeterminism, IdenticalSeedProducesBitwiseIdenticalResult) {
    const auto run = [] {
        return greeks::pathwise_european(100, 100, 0.05, 0.0, 0.2, 1.0, OptionType::Call,
                                          50'000, 909);
    };
    const auto a = run();
    const auto b = run();
    EXPECT_EQ(a.delta.value, b.delta.value);
    EXPECT_EQ(a.vega.value, b.vega.value);
    EXPECT_EQ(a.rho.value, b.rho.value);
}
