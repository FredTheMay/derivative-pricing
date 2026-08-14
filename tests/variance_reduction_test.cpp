#include "mcd/pricers/analytic.hpp"
#include "mcd/pricers/monte_carlo.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <cstdio>

using mcd::AverageStyle;
using mcd::BarrierDirection;
using mcd::BarrierKnock;
using mcd::OptionType;
using mcd::StrikeStyle;
namespace pricers = mcd::pricers;

namespace {
constexpr double kSpot = 100.0, kStrike = 100.0, kRate = 0.05, kCarry = 0.02, kVol = 0.25,
                 kTime = 1.0;
constexpr std::uint64_t kSeed = 777;

// Variance of the mean estimator, back out from the reported standard error:
// SE = sqrt(Var/N)  =>  Var = SE^2 * N.
double implied_variance(const pricers::McResult& r) {
    return r.standard_error * r.standard_error * static_cast<double>(r.path_count);
}
} // namespace

// --- Antithetic reduces variance, every product, matched total draws -----------------
// "Matched total draws": plain MC at N draws vs antithetic MC at N/2 pairs (N total
// underlying draws) -- the fair comparison for a genuine variance-reduction factor.

TEST(VarianceReduction, AntitheticReducesEuropeanVariance) {
    constexpr std::uint64_t n = 400'000;
    const auto plain = pricers::monte_carlo_european(kSpot, kStrike, kRate, kCarry, kVol, kTime,
                                                       OptionType::Call, n, kSeed);
    const auto anti = pricers::monte_carlo_european(kSpot, kStrike, kRate, kCarry, kVol, kTime,
                                                      OptionType::Call, n / 2, kSeed,
                                                      {.antithetic = true});
    const double factor = implied_variance(plain) / implied_variance(anti);
    std::printf("[VR] European antithetic factor: %.3f\n", factor);
    EXPECT_GT(factor, 1.0);
}

TEST(VarianceReduction, AntitheticReducesAsianVariance) {
    constexpr std::uint64_t n = 400'000;
    const auto plain = pricers::monte_carlo_asian(kSpot, kStrike, kRate, kCarry, kVol, kTime,
                                                    OptionType::Call, StrikeStyle::Fixed,
                                                    AverageStyle::Arithmetic, 50, n, kSeed);
    const auto anti = pricers::monte_carlo_asian(kSpot, kStrike, kRate, kCarry, kVol, kTime,
                                                   OptionType::Call, StrikeStyle::Fixed,
                                                   AverageStyle::Arithmetic, 50, n / 2, kSeed,
                                                   {.antithetic = true});
    const double factor = implied_variance(plain) / implied_variance(anti);
    std::printf("[VR] Asian antithetic factor: %.3f\n", factor);
    EXPECT_GT(factor, 1.0);
}

TEST(VarianceReduction, AntitheticReducesBarrierVariance) {
    constexpr std::uint64_t n = 400'000;
    const auto plain = pricers::monte_carlo_barrier(kSpot, kStrike, 80.0, kRate, kCarry, kVol,
                                                      kTime, OptionType::Call,
                                                      BarrierDirection::Down, BarrierKnock::Out,
                                                      0.0, 50, n, kSeed);
    const auto anti = pricers::monte_carlo_barrier(kSpot, kStrike, 80.0, kRate, kCarry, kVol,
                                                     kTime, OptionType::Call,
                                                     BarrierDirection::Down, BarrierKnock::Out,
                                                     0.0, 50, n / 2, kSeed, {.antithetic = true});
    const double factor = implied_variance(plain) / implied_variance(anti);
    std::printf("[VR] Barrier antithetic factor: %.3f\n", factor);
    EXPECT_GT(factor, 1.0);
}

TEST(VarianceReduction, AntitheticReducesLookbackVariance) {
    constexpr std::uint64_t n = 400'000;
    const auto plain = pricers::monte_carlo_lookback(kSpot, kStrike, kRate, kCarry, kVol, kTime,
                                                       OptionType::Call, StrikeStyle::Floating, 50,
                                                       n, kSeed);
    const auto anti = pricers::monte_carlo_lookback(kSpot, kStrike, kRate, kCarry, kVol, kTime,
                                                      OptionType::Call, StrikeStyle::Floating, 50,
                                                      n / 2, kSeed, {.antithetic = true});
    const double factor = implied_variance(plain) / implied_variance(anti);
    std::printf("[VR] Lookback antithetic factor: %.3f\n", factor);
    EXPECT_GT(factor, 1.0);
}

TEST(VarianceReduction, AntitheticReducesDigitalVariance) {
    constexpr std::uint64_t n = 400'000;
    const auto plain = pricers::monte_carlo_digital(kSpot, kStrike, kRate, kCarry, kVol, kTime,
                                                      OptionType::Call,
                                                      mcd::DigitalStyle::CashOrNothing, 1.0, n,
                                                      kSeed);
    const auto anti = pricers::monte_carlo_digital(kSpot, kStrike, kRate, kCarry, kVol, kTime,
                                                     OptionType::Call,
                                                     mcd::DigitalStyle::CashOrNothing, 1.0, n / 2,
                                                     kSeed, {.antithetic = true});
    const double factor = implied_variance(plain) / implied_variance(anti);
    std::printf("[VR] Digital antithetic factor: %.3f\n", factor);
    EXPECT_GT(factor, 1.0);
}

// --- Control variate reduces arithmetic-Asian variance by >= 5x, matched path_count --
// (Control variate costs no extra RNG draws -- same path_count is the fair comparison.)

TEST(VarianceReduction, ControlVariateReducesArithmeticAsianVarianceByAtLeast5x) {
    constexpr std::uint64_t n = 200'000;
    const auto plain = pricers::monte_carlo_asian(kSpot, kStrike, kRate, kCarry, kVol, kTime,
                                                    OptionType::Call, StrikeStyle::Fixed,
                                                    AverageStyle::Arithmetic, 50, n, kSeed);
    const auto controlled =
        pricers::monte_carlo_asian(kSpot, kStrike, kRate, kCarry, kVol, kTime, OptionType::Call,
                                    StrikeStyle::Fixed, AverageStyle::Arithmetic, 50, n, kSeed,
                                    {.control_variate = true});
    const double factor = implied_variance(plain) / implied_variance(controlled);
    std::printf("[VR] Arithmetic Asian control-variate factor: %.3f\n", factor);
    EXPECT_GE(factor, 5.0);
}

// --- Brownian bridge reduces discrete-monitoring bias, reported before/after ----------

TEST(VarianceReduction, BrownianBridgeReducesDiscreteMonitoringBias) {
    constexpr int monitoring_points = 12; // deliberately coarse, to have bias to correct
    constexpr std::uint64_t n = 300'000;
    const double barrier = 85.0;
    const double continuous_analytic =
        pricers::reiner_rubinstein(kSpot, kStrike, barrier, kRate, kCarry, kVol, kTime,
                                    OptionType::Call, BarrierDirection::Down, BarrierKnock::Out);

    const auto without_bridge =
        pricers::monte_carlo_barrier(kSpot, kStrike, barrier, kRate, kCarry, kVol, kTime,
                                      OptionType::Call, BarrierDirection::Down, BarrierKnock::Out,
                                      0.0, monitoring_points, n, kSeed);
    const auto with_bridge =
        pricers::monte_carlo_barrier(kSpot, kStrike, barrier, kRate, kCarry, kVol, kTime,
                                      OptionType::Call, BarrierDirection::Down, BarrierKnock::Out,
                                      0.0, monitoring_points, n, kSeed,
                                      {.brownian_bridge = true});

    const double bias_before = std::abs(without_bridge.price - continuous_analytic);
    const double bias_after = std::abs(with_bridge.price - continuous_analytic);
    std::printf("[VR] Brownian bridge bias: before=%.4f after=%.4f (continuous=%.4f)\n",
                bias_before, bias_after, continuous_analytic);
    EXPECT_LT(bias_after, bias_before);
}
