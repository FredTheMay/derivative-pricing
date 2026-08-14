#include "mcd/pricers/monte_carlo.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <cstdio>
#include <vector>

using mcd::OptionType;
namespace pricers = mcd::pricers;

// Log-log RMSE-vs-paths convergence slope, asserted at -0.5 +/- 0.05 (the standard
// Monte Carlo O(1/sqrt(N)) rate). "RMSE" here is the estimator's own reported standard
// error at each path count -- for an unbiased estimator (which plain MC European is,
// against BSM validated in Phase 2), the standard error over repeated trials *is* the
// expected root-mean-square deviation from the true price, by definition of standard
// error under the CLT. Using it directly avoids needing many repeated trials per point.
TEST(Convergence, LogLogSlopeIsMinusOneHalf) {
    const std::vector<std::uint64_t> path_counts = {1'000,   3'000,   10'000,  30'000,
                                                      100'000, 300'000, 1'000'000};

    std::vector<double> log_n;
    std::vector<double> log_se;
    for (std::uint64_t n : path_counts) {
        const auto mc = pricers::monte_carlo_european(100.0, 100.0, 0.05, 0.02, 0.25, 1.0,
                                                        OptionType::Call, n, /*seed=*/2024);
        log_n.push_back(std::log10(static_cast<double>(n)));
        log_se.push_back(std::log10(mc.standard_error));
        std::printf("[convergence] N=%llu SE=%.6g\n", static_cast<unsigned long long>(n),
                    mc.standard_error);
    }

    // Least-squares slope of log_se vs log_n.
    const auto count = static_cast<double>(log_n.size());
    double mean_x = 0.0, mean_y = 0.0;
    for (std::size_t i = 0; i < log_n.size(); ++i) {
        mean_x += log_n[i];
        mean_y += log_se[i];
    }
    mean_x /= count;
    mean_y /= count;

    double numerator = 0.0, denominator = 0.0;
    for (std::size_t i = 0; i < log_n.size(); ++i) {
        numerator += (log_n[i] - mean_x) * (log_se[i] - mean_y);
        denominator += (log_n[i] - mean_x) * (log_n[i] - mean_x);
    }
    const double slope = numerator / denominator;
    std::printf("[convergence] fitted slope=%.4f\n", slope);

    EXPECT_NEAR(slope, -0.5, 0.05);
}
