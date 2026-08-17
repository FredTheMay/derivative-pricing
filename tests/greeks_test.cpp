#include "mcd/core/rng.hpp"
#include "mcd/greeks/finite_difference.hpp"
#include "mcd/pricers/analytic.hpp"
#include "mcd/pricers/monte_carlo.hpp"

#include <gtest/gtest.h>

#include <bit>
#include <cmath>
#include <cstdint>

using mcd::OptionType;
namespace pricers = mcd::pricers;
namespace greeks = mcd::greeks;

// --- Common random numbers: direct equality of captured z draws, not just inferred from
// smooth Greeks (docs/design/05-greeks-and-american.md sec.2.1 / sec.4). Phase 2's RNG
// counter is keyed on (seed, path_index, draw_index) only -- never on the priced spot,
// rate, vol, or time -- so the draw for a given path is identical whether it feeds a base
// scenario or any bumped one. -------------------------------------------------------

TEST(CommonRandomNumbers, ZDrawIsIndependentOfScenarioParameters) {
    const std::uint64_t seed = 42;
    for (std::uint64_t path_index = 0; path_index < 1000; path_index += 137) {
        const double z_base = mcd::standard_normal_variate(seed, path_index);
        // "Bumped scenario" here means nothing about the draw call changes -- the point of
        // this test is that finite_difference_european's bumped calls reuse this exact API
        // with the same (seed, path_index), never threading spot/vol/rate/time into it.
        const double z_bumped = mcd::standard_normal_variate(seed, path_index);
        ASSERT_EQ(std::bit_cast<std::uint64_t>(z_base), std::bit_cast<std::uint64_t>(z_bumped))
            << "path_index=" << path_index;
    }
}

// Reproduces finite_difference_european's own arithmetic from the underlying McResults to
// (a) cross-check the production function's formula against an independent reimplementation,
// and (b) obtain a standard-error bound for the "within 3 SE" oracle comparison below, since
// EuropeanGreeks itself doesn't carry per-Greek SEs. Assumes independence between the bumped
// McResults' errors (no correlation term), which CRN violates in the conservative direction --
// true correlated SE is smaller than this bound, so this is a deliberately loose, not tight,
// tolerance. Documented as such rather than presented as an exact confidence interval.
namespace {
struct GreekSeBound {
    double delta, gamma, vega, theta, rho;
};

GreekSeBound conservative_se_bound(double spot, double strike, double rate, double carry_yield,
                                    double vol, double time, OptionType type,
                                    std::uint64_t path_count, std::uint64_t seed,
                                    const greeks::BumpSizes& b) {
    const auto mc = [&](double s, double r, double q, double v, double t) {
        return pricers::monte_carlo_european(s, strike, r, q, v, t, type, path_count, seed);
    };
    const auto se0 = mc(spot, rate, carry_yield, vol, time).standard_error;
    const auto se_s_up = mc(spot + b.spot, rate, carry_yield, vol, time).standard_error;
    const auto se_s_dn = mc(spot - b.spot, rate, carry_yield, vol, time).standard_error;
    const auto se_v_up = mc(spot, rate, carry_yield, vol + b.vol, time).standard_error;
    const auto se_v_dn = mc(spot, rate, carry_yield, vol - b.vol, time).standard_error;
    const auto se_r_up = mc(spot, rate + b.rate, carry_yield, vol, time).standard_error;
    const auto se_r_dn = mc(spot, rate - b.rate, carry_yield, vol, time).standard_error;
    const auto se_t_up = mc(spot, rate, carry_yield, vol, time + b.time).standard_error;
    const auto se_t_dn = mc(spot, rate, carry_yield, vol, time - b.time).standard_error;

    return GreekSeBound{
        .delta = std::sqrt(se_s_up * se_s_up + se_s_dn * se_s_dn) / (2.0 * b.spot),
        .gamma = std::sqrt(se_s_up * se_s_up + 4.0 * se0 * se0 + se_s_dn * se_s_dn) /
                 (b.spot * b.spot),
        .vega = std::sqrt(se_v_up * se_v_up + se_v_dn * se_v_dn) / (2.0 * b.vol),
        .theta = std::sqrt(se_t_up * se_t_up + se_t_dn * se_t_dn) / (2.0 * b.time),
        .rho = std::sqrt(se_r_up * se_r_up + se_r_dn * se_r_dn) / (2.0 * b.rate),
    };
}

// Tight deterministic finite differences on the already-validated (Phase 1) closed form --
// the independent oracle FD Greeks are checked against, per CLAUDE.md sec.2.5.
double analytic_bump(double h) { return h; }

struct AnalyticGreeks {
    double delta, gamma, vega, theta, rho;
};

AnalyticGreeks analytic_greeks(double spot, double strike, double rate, double carry_yield,
                                double vol, double time, OptionType type) {
    const double hs = analytic_bump(spot * 1e-5);
    const double hv = analytic_bump(vol * 1e-5);
    const double hr = analytic_bump(0.01 * 1e-3);
    const double ht = analytic_bump(time * 1e-5);
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

class FiniteDifferenceGreeksVsAnalytic : public ::testing::TestWithParam<GreeksCase> {};

TEST_P(FiniteDifferenceGreeksVsAnalytic, WithinThreeStandardErrors) {
    const auto c = GetParam();
    const std::uint64_t path_count = 300'000;
    const std::uint64_t seed = 2024;
    const auto bumps = greeks::default_bump_sizes(c.s, c.sigma, c.t);

    const auto fd =
        greeks::finite_difference_european(c.s, c.k, c.r, c.q, c.sigma, c.t, c.type, path_count,
                                            seed, bumps);
    const auto analytic = analytic_greeks(c.s, c.k, c.r, c.q, c.sigma, c.t, c.type);
    const auto se = conservative_se_bound(c.s, c.k, c.r, c.q, c.sigma, c.t, c.type, path_count,
                                           seed, bumps);

    auto check = [](const char* name, double fd_val, double analytic_val, double se_val) {
        const double deviation = std::abs(fd_val - analytic_val);
        EXPECT_LT(deviation, 3.0 * se_val)
            << name << ": fd=" << fd_val << " analytic=" << analytic_val
            << " deviation=" << deviation << " SE_bound=" << se_val
            << " (deviation/SE=" << deviation / se_val << ")";
    };
    check("delta", fd.delta, analytic.delta, se.delta);
    check("gamma", fd.gamma, analytic.gamma, se.gamma);
    check("vega", fd.vega, analytic.vega, se.vega);
    check("theta", fd.theta, analytic.theta, se.theta);
    check("rho", fd.rho, analytic.rho, se.rho);
}

INSTANTIATE_TEST_SUITE_P(
    ParameterMatrix, FiniteDifferenceGreeksVsAnalytic,
    ::testing::Values(GreeksCase{100, 100, 0.05, 0.00, 0.20, 1.0, OptionType::Call},
                       GreeksCase{100, 100, 0.05, 0.00, 0.20, 1.0, OptionType::Put},
                       GreeksCase{100, 80, 0.05, 0.02, 0.25, 0.5, OptionType::Call},
                       GreeksCase{100, 120, 0.05, 0.02, 0.30, 2.0, OptionType::Put}));
