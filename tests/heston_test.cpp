#include "mcd/pricers/heston.hpp"

#include "mcd/pricers/analytic.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <complex>
#include <cstdio>
#include <vector>

using mcd::OptionType;
using mcd::models::HestonParams;
namespace pricers = mcd::pricers;

namespace {

// Alan Lewis's published high-precision Heston reference prices
// (financepress.com/2019/02/15/heston-model-reference-prices/, "computed in
// Mathematica to high precision... confirmed by others to at least 15-16 good
// digits"), fetched this session per docs/design/12-heston.md sec.5 item 3 -- not
// recalled from memory, per CLAUDE.md sec.2.5. Lewis's own SDE notation there is
// dV = (omega - theta_L*V)dt + xi*sqrt(V)dW, i.e. his theta_L is THIS project's
// kappa (mean-reversion speed) and his omega is kappa*theta in this project's
// notation (theta = omega/theta_L = 1/4 = 0.25) -- confirmed by matching his SDE's
// linear-in-V coefficient against mcd::models::HestonParams's own dv =
// kappa*(theta-v)dt + xi*sqrt(v)dW convention. Parameters: S0=100, r=0.01, q=0.02,
// T=1, v0=0.04, kappa=4 (Lewis's theta), theta=0.25 (Lewis's omega/theta), xi=1,
// rho=-0.5.
const HestonParams kLewisReferenceParams{.spot = 100.0,
                                          .rate = 0.01,
                                          .carry_yield = 0.02,
                                          .v0 = 0.04,
                                          .kappa = 4.0,
                                          .theta = 0.25,
                                          .xi = 1.0,
                                          .rho = -0.5,
                                          .time = 1.0};

struct LewisCase {
    double strike;
    double call_price;
};

const std::vector<LewisCase> kLewisReferenceCases = {
    {80.0, 26.774758743998854221},
    {90.0, 20.933349000596710388},
    {100.0, 16.070154917028834278},
    {110.0, 12.132211516709844868},
    {120.0, 9.024913483457835637},
};

} // namespace

TEST(Heston, SemiAnalyticMatchesPublishedReferencePrices) {
    for (const auto& c : kLewisReferenceCases) {
        const double price =
            pricers::heston_semi_analytic(kLewisReferenceParams, c.strike, OptionType::Call);
        EXPECT_NEAR(price, c.call_price, 1e-9)
            << "strike=" << c.strike << " expected=" << c.call_price << " got=" << price;
    }
}

TEST(Heston, PutCallParityHoldsOnSemiAnalyticPrice) {
    for (const auto& c : kLewisReferenceCases) {
        const double call =
            pricers::heston_semi_analytic(kLewisReferenceParams, c.strike, OptionType::Call);
        const double put =
            pricers::heston_semi_analytic(kLewisReferenceParams, c.strike, OptionType::Put);
        const double discount_q =
            std::exp(-kLewisReferenceParams.carry_yield * kLewisReferenceParams.time);
        const double discount_r =
            std::exp(-kLewisReferenceParams.rate * kLewisReferenceParams.time);
        const double rhs =
            kLewisReferenceParams.spot * discount_q - c.strike * discount_r;
        EXPECT_NEAR(call - put, rhs, 1e-9) << "strike=" << c.strike;
    }
}

// Self-contained limiting case, no external data (docs/design/12-heston.md sec.5
// item 1): as xi -> 0 with v0 == theta, variance stops moving and Heston must reduce
// to Black-Scholes-Merton with sigma = sqrt(theta).
//
// xi = 1e-4, not smaller: the characteristic function divides by xi^2 (see
// heston_char_function), so pushing xi much below this trades a shrinking true
// approximation error for growing floating-point cancellation error -- measured
// directly (a throwaway script swept xi from 1e-2 to 1e-7): the heston-vs-BSM gap
// shrinks monotonically from 1e-2 down to ~1e-5, bottoms out around 1e-6 error at
// xi=1e-5, then *grows* again below that, reaching 2.4e-2 by xi=1e-7 -- the same
// truncation-vs-noise floor shape as Phase 5's finite-difference bump-size study,
// here between the model's true xi->0 limit and this formula's own numerical
// conditioning rather than Monte Carlo noise.
TEST(Heston, ReducesToBlackScholesAsVolOfVolVanishes) {
    const double theta = 0.09; // sigma = 0.3
    const HestonParams params{.spot = 100.0,
                               .rate = 0.05,
                               .carry_yield = 0.02,
                               .v0 = theta,
                               .kappa = 2.0,
                               .theta = theta,
                               .xi = 1e-4,
                               .rho = -0.3,
                               .time = 1.0};
    const double heston_price = pricers::heston_semi_analytic(params, 100.0, OptionType::Call);
    const double bsm_price = pricers::black_scholes_merton(
        params.spot, 100.0, params.rate, params.carry_yield, std::sqrt(theta), params.time,
        OptionType::Call);
    EXPECT_NEAR(heston_price, bsm_price, 1e-4);
}

TEST(Heston, QeMonteCarloWithinThreeStandardErrorsOfSemiAnalytic) {
    struct Case {
        HestonParams params;
        double strike;
        const char* label;
    };
    const std::vector<Case> cases = {
        // Feller satisfied (2*kappa*theta > xi^2): 2*4*0.25=2 > 1.
        {kLewisReferenceParams, 100.0, "Lewis reference (Feller satisfied)"},
        // Feller violated (2*kappa*theta < xi^2): 2*0.5*0.04=0.04 < 0.09 -- forces
        // real coverage of the QE high-noise (psi > psi_c) branch.
        {HestonParams{.spot = 100.0, .rate = 0.03, .carry_yield = 0.0, .v0 = 0.04,
                       .kappa = 0.5, .theta = 0.04, .xi = 0.3, .rho = -0.7, .time = 1.0},
         100.0, "sub-Feller"},
    };

    for (const auto& c : cases) {
        const auto mc = pricers::heston_qe_european(c.params, c.strike, OptionType::Call,
                                                      /*monitoring_points=*/50,
                                                      /*path_count=*/200'000, /*seed=*/2024);
        const double analytic =
            pricers::heston_semi_analytic(c.params, c.strike, OptionType::Call);
        std::printf("[heston] %s: qe=%.6f (SE=%.6f) analytic=%.6f dev=%.2f SE\n", c.label,
                    mc.price, mc.standard_error, analytic,
                    std::abs(mc.price - analytic) / mc.standard_error);
        EXPECT_LT(std::abs(mc.price - analytic), 3.0 * mc.standard_error)
            << c.label << ": qe=" << mc.price << " SE=" << mc.standard_error
            << " analytic=" << analytic;
    }
}

TEST(Heston, VarianceNeverNegativeOrNanUnderExtremeSubFeller) {
    // 2*kappa*theta = 2*0.3*0.02 = 0.012, xi^2 = 0.64 -- deeply sub-Feller, forcing
    // heavy use of the QE high-noise branch. This is exactly the regime QE exists to
    // handle correctly (Andersen 2008); a naive scheme would go negative here.
    const HestonParams params{.spot = 100.0, .rate = 0.02, .carry_yield = 0.0, .v0 = 0.02,
                               .kappa = 0.3, .theta = 0.02, .xi = 0.8, .rho = -0.6, .time = 1.0};
    const auto mc = pricers::heston_qe_european(params, 100.0, OptionType::Call,
                                                  /*monitoring_points=*/100,
                                                  /*path_count=*/500'000, /*seed=*/7);
    EXPECT_FALSE(std::isnan(mc.price));
    EXPECT_GE(mc.price, 0.0);
    EXPECT_FALSE(std::isnan(mc.standard_error));
}

TEST(Heston, DeterministicAcrossRepeatedRuns) {
    const auto a = pricers::heston_qe_european(kLewisReferenceParams, 100.0, OptionType::Call,
                                                20, 10'000, 42);
    const auto b = pricers::heston_qe_european(kLewisReferenceParams, 100.0, OptionType::Call,
                                                20, 10'000, 42);
    EXPECT_EQ(a.price, b.price);
    EXPECT_EQ(a.standard_error, b.standard_error);
}

TEST(Heston, BiasShrinksAsStepCountIncreases) {
    const double analytic =
        pricers::heston_semi_analytic(kLewisReferenceParams, 100.0, OptionType::Call);
    double previous_abs_bias = 1e18;
    for (int steps : {5, 20, 100}) {
        const auto mc = pricers::heston_qe_european(kLewisReferenceParams, 100.0,
                                                      OptionType::Call, steps, 300'000, 99);
        const double bias = std::abs(mc.price - analytic);
        std::printf("[heston bias] steps=%d bias=%.6f SE=%.6f\n", steps, bias,
                    mc.standard_error);
        // Bias should not be growing as discretization refines; generous margin
        // (SE-scaled) since this is a noisy MC comparison, not an exact monotone
        // sequence.
        EXPECT_LT(bias, previous_abs_bias + 3.0 * mc.standard_error);
        previous_abs_bias = bias;
    }
}
