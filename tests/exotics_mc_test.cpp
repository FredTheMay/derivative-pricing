#include "mcd/pricers/analytic.hpp"
#include "mcd/pricers/monte_carlo.hpp"

#include <gtest/gtest.h>

#include <cmath>

using mcd::AverageStyle;
using mcd::BarrierDirection;
using mcd::BarrierKnock;
using mcd::DigitalStyle;
using mcd::OptionType;
using mcd::StrikeStyle;
namespace pricers = mcd::pricers;

namespace {
constexpr double kSpot = 100.0, kStrike = 100.0, kRate = 0.05, kCarry = 0.02, kVol = 0.25,
                 kTime = 1.0;
constexpr std::uint64_t kSeed = 1234;
}

// --- Geometric Asian MC within 3 SE of Kemna-Vorst -----------------------------------

TEST(ExoticsMc, GeometricAsianWithinThreeSE) {
    // monitoring_points=400: kemna_vorst prices *continuous* geometric averaging, so a
    // coarser discrete grid carries a small discretization bias of its own (same root
    // cause as the barrier/lookback discretization bias below, just much smaller in
    // magnitude for a running-average payoff than for an extremum-tracking one).
    for (OptionType type : {OptionType::Call, OptionType::Put}) {
        const auto mc = pricers::monte_carlo_asian(kSpot, kStrike, kRate, kCarry, kVol, kTime,
                                                     type, StrikeStyle::Fixed,
                                                     AverageStyle::Geometric,
                                                     /*monitoring_points=*/400,
                                                     /*path_count=*/200'000, kSeed);
        const double analytic = pricers::kemna_vorst(kSpot, kStrike, kRate, kCarry, kVol, kTime,
                                                       type);
        EXPECT_LT(std::abs(mc.price - analytic), 3.0 * mc.standard_error)
            << "mc=" << mc.price << " analytic=" << analytic << " SE=" << mc.standard_error;
    }
}

// --- Barrier MC within 3 SE of Reiner-Rubinstein as monitoring -> continuous ----------

TEST(ExoticsMc, BarrierConvergesToContinuousReinerRubinstein) {
    const double barrier = 80.0;
    const double analytic = pricers::reiner_rubinstein(kSpot, kStrike, barrier, kRate, kCarry,
                                                         kVol, kTime, OptionType::Call,
                                                         BarrierDirection::Down,
                                                         BarrierKnock::Out);
    double previous_error = 1e300;
    for (int monitoring_points : {4, 16, 64, 252}) {
        const auto mc = pricers::monte_carlo_barrier(kSpot, kStrike, barrier, kRate, kCarry, kVol,
                                                       kTime, OptionType::Call,
                                                       BarrierDirection::Down, BarrierKnock::Out,
                                                       /*rebate=*/0.0, monitoring_points,
                                                       /*path_count=*/100'000, kSeed);
        const double error = std::abs(mc.price - analytic);
        EXPECT_LT(error, previous_error) << "monitoring_points=" << monitoring_points;
        previous_error = error;
    }
}

// --- Lookback MC converges to Phase 1 closed forms as monitoring -> continuous -------
// Lookback's running-extremum payoff has the same discrete-vs-continuous-monitoring
// discretization bias as barriers (verified empirically this session: bias shrinks with
// monitoring_points at very close to the O(1/sqrt(monitoring_points)) rate theory
// predicts -- an 8x increase in monitoring_points shrank the bias by 2.87x against a
// predicted sqrt(8)=2.83x). Goldman-Sosin-Gatto-style closed forms price *continuous*
// monitoring, so -- exactly as CLAUDE.md already frames the barrier test -- this is a
// convergence claim, not a fixed-monitoring 3 SE match.

TEST(ExoticsMc, LookbackFixedConvergesToClosedForm) {
    for (OptionType type : {OptionType::Call, OptionType::Put}) {
        const double analytic =
            pricers::lookback_fixed_strike(kSpot, kStrike, kRate, kCarry, kVol, kTime, type);
        double previous_error = 1e300;
        for (int monitoring_points : {50, 200, 1000, 4000}) {
            const auto mc = pricers::monte_carlo_lookback(kSpot, kStrike, kRate, kCarry, kVol,
                                                            kTime, type, StrikeStyle::Fixed,
                                                            monitoring_points,
                                                            /*path_count=*/50'000, kSeed);
            const double error = std::abs(mc.price - analytic);
            EXPECT_LT(error, previous_error) << "type=" << static_cast<int>(type)
                                              << " monitoring_points=" << monitoring_points;
            previous_error = error;
        }
    }
}

TEST(ExoticsMc, LookbackFloatingConvergesToClosedForm) {
    for (OptionType type : {OptionType::Call, OptionType::Put}) {
        const double analytic =
            pricers::lookback_floating_strike(kSpot, kRate, kCarry, kVol, kTime, type);
        double previous_error = 1e300;
        for (int monitoring_points : {50, 200, 1000, 4000}) {
            const auto mc = pricers::monte_carlo_lookback(kSpot, kStrike, kRate, kCarry, kVol,
                                                            kTime, type, StrikeStyle::Floating,
                                                            monitoring_points,
                                                            /*path_count=*/50'000, kSeed);
            const double error = std::abs(mc.price - analytic);
            EXPECT_LT(error, previous_error) << "type=" << static_cast<int>(type)
                                              << " monitoring_points=" << monitoring_points;
            previous_error = error;
        }
    }
}

// --- In + out parity at the MC level, all four direction/type pairs ------------------

class BarrierMcParityTest
    : public ::testing::TestWithParam<std::tuple<BarrierDirection, OptionType, double>> {};

TEST_P(BarrierMcParityTest, InPlusOutEqualsVanillaWithinCombinedSE) {
    const auto [direction, type, barrier] = GetParam();
    const auto in_result =
        pricers::monte_carlo_barrier(kSpot, kStrike, barrier, kRate, kCarry, kVol, kTime, type,
                                      direction, BarrierKnock::In, 0.0, 100, 200'000, kSeed);
    const auto out_result =
        pricers::monte_carlo_barrier(kSpot, kStrike, barrier, kRate, kCarry, kVol, kTime, type,
                                      direction, BarrierKnock::Out, 0.0, 100, 200'000, kSeed);
    const auto vanilla =
        pricers::monte_carlo_european(kSpot, kStrike, kRate, kCarry, kVol, kTime, type, 200'000,
                                       kSeed);

    const double combined_se =
        std::sqrt(in_result.standard_error * in_result.standard_error +
                   out_result.standard_error * out_result.standard_error +
                   vanilla.standard_error * vanilla.standard_error);
    EXPECT_LT(std::abs((in_result.price + out_result.price) - vanilla.price), 3.0 * combined_se);
}

INSTANTIATE_TEST_SUITE_P(
    AllBarrierTypes, BarrierMcParityTest,
    ::testing::Values(std::make_tuple(BarrierDirection::Down, OptionType::Call, 80.0),
                       std::make_tuple(BarrierDirection::Down, OptionType::Put, 80.0),
                       std::make_tuple(BarrierDirection::Up, OptionType::Call, 120.0),
                       std::make_tuple(BarrierDirection::Up, OptionType::Put, 120.0)));

// --- Digital: within 3 SE of the Phase 1 analytic formula, and against a replicating
// call-spread bound (buy K-dK/2 call, sell K+dK/2 call, scaled by 1/dK) ---------------

TEST(ExoticsMc, DigitalWithinThreeSEOfAnalytic) {
    const auto mc = pricers::monte_carlo_digital(kSpot, kStrike, kRate, kCarry, kVol, kTime,
                                                   OptionType::Call, DigitalStyle::CashOrNothing,
                                                   1.0, 500'000, kSeed);
    const double analytic = pricers::digital(kSpot, kStrike, kRate, kCarry, kVol, kTime,
                                              OptionType::Call, DigitalStyle::CashOrNothing, 1.0);
    EXPECT_LT(std::abs(mc.price - analytic), 3.0 * mc.standard_error);
}

TEST(ExoticsMc, DigitalMatchesReplicatingCallSpread) {
    const double dk = 0.02; // small relative to strike=100
    const double spread_price =
        (pricers::black_scholes_merton(kSpot, kStrike - dk / 2.0, kRate, kCarry, kVol, kTime,
                                        OptionType::Call) -
         pricers::black_scholes_merton(kSpot, kStrike + dk / 2.0, kRate, kCarry, kVol, kTime,
                                        OptionType::Call)) /
        dk;
    const double analytic = pricers::digital(kSpot, kStrike, kRate, kCarry, kVol, kTime,
                                              OptionType::Call, DigitalStyle::CashOrNothing, 1.0);
    EXPECT_NEAR(spread_price, analytic, 1e-3);
}

// --- Asian value <= corresponding European value (averaging reduces volatility) ------

TEST(ExoticsMc, ArithmeticAsianValueBelowEuropean) {
    const auto asian = pricers::monte_carlo_asian(kSpot, kStrike, kRate, kCarry, kVol, kTime,
                                                    OptionType::Call, StrikeStyle::Fixed,
                                                    AverageStyle::Arithmetic, 100, 200'000, kSeed);
    const auto european = pricers::monte_carlo_european(kSpot, kStrike, kRate, kCarry, kVol,
                                                          kTime, OptionType::Call, 200'000, kSeed);
    const double combined_se =
        std::sqrt(asian.standard_error * asian.standard_error +
                   european.standard_error * european.standard_error);
    EXPECT_LT(asian.price, european.price + 3.0 * combined_se);
}
