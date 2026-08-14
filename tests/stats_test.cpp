#include "mcd/core/rng.hpp"
#include "mcd/core/stats.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <vector>

TEST(WelfordAccumulator, MatchesNaiveTwoPassOnOneMillionSamples) {
    constexpr std::uint64_t n = 1'000'000;
    std::vector<double> samples;
    samples.reserve(n);

    mcd::WelfordAccumulator accumulator;
    for (std::uint64_t i = 0; i < n; ++i) {
        const double z = mcd::standard_normal_variate(/*seed=*/99, i);
        samples.push_back(z);
        accumulator.add(z);
    }

    double sum = 0.0;
    for (double s : samples) {
        sum += s;
    }
    const double naive_mean = sum / static_cast<double>(n);

    double sum_sq_dev = 0.0;
    for (double s : samples) {
        sum_sq_dev += (s - naive_mean) * (s - naive_mean);
    }
    const double naive_variance = sum_sq_dev / static_cast<double>(n - 1);

    EXPECT_NEAR(accumulator.mean(), naive_mean, 1e-12);
    EXPECT_NEAR(accumulator.variance(), naive_variance, 1e-12);
    EXPECT_EQ(accumulator.count(), n);
}

TEST(WelfordAccumulator, StandardErrorIsSqrtVarianceOverN) {
    mcd::WelfordAccumulator accumulator;
    for (double v : {1.0, 2.0, 3.0, 4.0, 5.0}) {
        accumulator.add(v);
    }
    EXPECT_NEAR(accumulator.standard_error(),
                std::sqrt(accumulator.variance() / static_cast<double>(accumulator.count())),
                1e-15);
}
