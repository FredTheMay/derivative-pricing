#include "mcd/pricers/qmc.hpp"

#include "mcd/pricers/analytic.hpp"
#include "mcd/pricers/monte_carlo.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <cstdio>
#include <vector>

using mcd::OptionType;
using mcd::StrikeStyle;
namespace pricers = mcd::pricers;

// qmc_sobol_european within a fixed, deterministic tolerance of BSM analytic. No
// standard error to compare against (QmcResult has none, by design -- see
// docs/design/10-sobol-qmc.md sec.6) so the tolerance is a fixed number, justified by
// the measured convergence rate demonstrated in QmcConvergence.LogLogSlopeBeatsPlainMc
// below rather than asserted blind.
TEST(Qmc, EuropeanWithinToleranceOfAnalytic) {
    struct Case {
        double spot, strike, rate, carry_yield, vol, time;
        OptionType type;
    };
    const std::vector<Case> cases = {
        {100.0, 100.0, 0.05, 0.02, 0.20, 1.0, OptionType::Call},
        {100.0, 110.0, 0.03, 0.00, 0.30, 0.5, OptionType::Put},
        {80.0, 100.0, 0.05, 0.01, 0.25, 2.0, OptionType::Call},
    };
    constexpr std::uint64_t kPathCount = 1'000'000;

    for (const auto& c : cases) {
        const double analytic =
            pricers::black_scholes_merton(c.spot, c.strike, c.rate, c.carry_yield, c.vol, c.time,
                                           c.type);
        const auto qmc = pricers::qmc_sobol_european(c.spot, c.strike, c.rate, c.carry_yield,
                                                       c.vol, c.time, c.type, kPathCount);
        EXPECT_NEAR(qmc.price, analytic, 0.02)
            << "spot=" << c.spot << " strike=" << c.strike << " analytic=" << analytic
            << " qmc=" << qmc.price;
    }
}

// qmc_sobol_asian cross-checked against Phase 3's plain-MC arithmetic Asian pricer at a
// shared, generous tolerance -- no independent closed form exists for arithmetic Asian
// (same situation Phase 3's own MC pricer was validated under).
TEST(Qmc, AsianMatchesPlainMonteCarloWithinTolerance) {
    const double spot = 100.0, strike = 100.0, rate = 0.05, carry_yield = 0.02, vol = 0.25,
                 time = 1.0;
    const int monitoring_points = 7; // the Stretch-Goal-3 dimension cap
    constexpr std::uint64_t kPathCount = 500'000;

    const auto mc = pricers::monte_carlo_asian(spot, strike, rate, carry_yield, vol, time,
                                                 OptionType::Call, StrikeStyle::Fixed,
                                                 mcd::AverageStyle::Arithmetic, monitoring_points,
                                                 kPathCount, /*seed=*/2024);
    const auto qmc = pricers::qmc_sobol_asian(spot, strike, rate, carry_yield, vol, time,
                                               OptionType::Call, StrikeStyle::Fixed,
                                               monitoring_points, kPathCount);

    std::printf("[qmc asian] plain_mc=%.6f (SE=%.6f) qmc=%.6f\n", mc.price, mc.standard_error,
                qmc.price);
    EXPECT_NEAR(qmc.price, mc.price, 3.0 * mc.standard_error + 0.05);
}

namespace {

double log_log_slope(const std::vector<double>& log_n, const std::vector<double>& log_err) {
    const auto count = static_cast<double>(log_n.size());
    double mean_x = 0.0, mean_y = 0.0;
    for (std::size_t i = 0; i < log_n.size(); ++i) {
        mean_x += log_n[i];
        mean_y += log_err[i];
    }
    mean_x /= count;
    mean_y /= count;

    double numerator = 0.0, denominator = 0.0;
    for (std::size_t i = 0; i < log_n.size(); ++i) {
        numerator += (log_n[i] - mean_x) * (log_err[i] - mean_y);
        denominator += (log_n[i] - mean_x) * (log_n[i] - mean_x);
    }
    return numerator / denominator;
}

} // namespace

// The stretch goal's actual deliverable (docs/design/10-sobol-qmc.md sec.6): Sobol's
// log-log absolute-error-vs-path-count slope must be steeper (more negative) than plain
// MC's -0.5, at matched path counts, for the same European option. Real measured data,
// reported and asserted whichever way it comes out.
TEST(QmcConvergence, LogLogSlopeBeatsPlainMonteCarlo) {
    const double spot = 100.0, strike = 100.0, rate = 0.05, carry_yield = 0.02, vol = 0.20,
                 time = 1.0;
    const OptionType type = OptionType::Call;
    const double analytic =
        pricers::black_scholes_merton(spot, strike, rate, carry_yield, vol, time, type);

    const std::vector<std::uint64_t> path_counts = {1'000,   3'000,   10'000,  30'000,
                                                      100'000, 300'000, 1'000'000};

    std::vector<double> log_n, log_err_mc, log_err_qmc;
    for (std::uint64_t n : path_counts) {
        const auto mc =
            pricers::monte_carlo_european(spot, strike, rate, carry_yield, vol, time, type, n,
                                           /*seed=*/2024);
        const auto qmc =
            pricers::qmc_sobol_european(spot, strike, rate, carry_yield, vol, time, type, n);

        const double mc_err = std::abs(mc.price - analytic);
        const double qmc_err = std::abs(qmc.price - analytic);

        log_n.push_back(std::log10(static_cast<double>(n)));
        log_err_mc.push_back(std::log10(mc_err));
        log_err_qmc.push_back(std::log10(qmc_err));

        std::printf("[qmc convergence] N=%llu plain_mc_err=%.3e qmc_err=%.3e\n",
                    static_cast<unsigned long long>(n), mc_err, qmc_err);
    }

    const double slope_mc = log_log_slope(log_n, log_err_mc);
    const double slope_qmc = log_log_slope(log_n, log_err_qmc);
    std::printf("[qmc convergence] plain_mc slope=%.4f qmc slope=%.4f\n", slope_mc, slope_qmc);

    EXPECT_LT(slope_qmc, slope_mc);
    EXPECT_LT(slope_qmc, -0.5);
}
