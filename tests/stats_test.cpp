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

TEST(WelfordAccumulator, MergeWithEmptyIsIdentity) {
    mcd::WelfordAccumulator a;
    for (double v : {1.0, 2.0, 3.0, 4.0, 5.0}) {
        a.add(v);
    }
    const mcd::WelfordAccumulator empty;

    mcd::WelfordAccumulator a_merge_empty = a;
    a_merge_empty.merge(empty);
    EXPECT_EQ(a_merge_empty.count(), a.count());
    EXPECT_EQ(a_merge_empty.mean(), a.mean());
    EXPECT_EQ(a_merge_empty.variance(), a.variance());

    mcd::WelfordAccumulator empty_merge_a = empty;
    empty_merge_a.merge(a);
    EXPECT_EQ(empty_merge_a.count(), a.count());
    EXPECT_EQ(empty_merge_a.mean(), a.mean());
    EXPECT_EQ(empty_merge_a.variance(), a.variance());
}

TEST(WelfordAccumulator, MergeMatchesNaiveTwoPassOnCombinedData) {
    constexpr std::uint64_t n_a = 300'000;
    constexpr std::uint64_t n_b = 500'000;
    std::vector<double> all_samples;
    all_samples.reserve(n_a + n_b);

    mcd::WelfordAccumulator acc_a;
    for (std::uint64_t i = 0; i < n_a; ++i) {
        const double z = mcd::standard_normal_variate(/*seed=*/7, i);
        acc_a.add(z);
        all_samples.push_back(z);
    }
    mcd::WelfordAccumulator acc_b;
    for (std::uint64_t i = 0; i < n_b; ++i) {
        // Different path-index range, mirroring how two chunks partition disjoint paths.
        const double z = mcd::standard_normal_variate(/*seed=*/7, n_a + i);
        acc_b.add(z);
        all_samples.push_back(z);
    }

    acc_a.merge(acc_b);

    double sum = 0.0;
    for (double s : all_samples) {
        sum += s;
    }
    const double naive_mean = sum / static_cast<double>(all_samples.size());
    double sum_sq_dev = 0.0;
    for (double s : all_samples) {
        sum_sq_dev += (s - naive_mean) * (s - naive_mean);
    }
    const double naive_variance = sum_sq_dev / static_cast<double>(all_samples.size() - 1);

    EXPECT_EQ(acc_a.count(), n_a + n_b);
    EXPECT_NEAR(acc_a.mean(), naive_mean, 1e-9);
    EXPECT_NEAR(acc_a.variance(), naive_variance, 1e-9);
}
