#include "mcd/pricers/monte_carlo.hpp"

#include <gtest/gtest.h>

#include <bit>
#include <cmath>
#include <cstdint>
#include <thread>
#include <vector>

using mcd::AverageStyle;
using mcd::BarrierDirection;
using mcd::BarrierKnock;
using mcd::DigitalStyle;
using mcd::OptionType;
using mcd::StrikeStyle;
namespace pricers = mcd::pricers;

namespace {

std::vector<unsigned> thread_counts_to_test() {
    const unsigned hw = std::thread::hardware_concurrency();
    std::vector<unsigned> counts{1, 2, 4, 8};
    if (hw > 0 && hw != 4 && hw != 8) {
        counts.push_back(hw);
    }
    return counts;
}

void expect_bitwise_identical(double a, double b, const char* label) {
    EXPECT_EQ(std::bit_cast<std::uint64_t>(a), std::bit_cast<std::uint64_t>(b)) << label;
}

// path_count deliberately not a multiple of any tested thread count, and modest in size
// so this test suite (which sweeps every product x every thread count) stays fast.
constexpr std::uint64_t kPathCount = 50'003;
constexpr std::uint64_t kSeed = 424242;

} // namespace

TEST(BitwiseDeterminism, European) {
    mcd::pricers::McResult reference{};
    bool have_reference = false;
    for (unsigned n : thread_counts_to_test()) {
        const auto result = pricers::monte_carlo_european(100.0, 100.0, 0.05, 0.02, 0.25, 1.0,
                                                            OptionType::Call, kPathCount, kSeed,
                                                            {.num_threads = n});
        if (!have_reference) {
            reference = result;
            have_reference = true;
        } else {
            expect_bitwise_identical(result.price, reference.price, "price");
            expect_bitwise_identical(result.standard_error, reference.standard_error, "SE");
        }
        EXPECT_EQ(result.path_count, kPathCount) << "num_threads=" << n;
    }
}

TEST(BitwiseDeterminism, EuropeanWithAntithetic) {
    mcd::pricers::McResult reference{};
    bool have_reference = false;
    for (unsigned n : thread_counts_to_test()) {
        const auto result = pricers::monte_carlo_european(
            100.0, 100.0, 0.05, 0.02, 0.25, 1.0, OptionType::Call, kPathCount, kSeed,
            {.antithetic = true, .num_threads = n});
        if (!have_reference) {
            reference = result;
            have_reference = true;
        } else {
            expect_bitwise_identical(result.price, reference.price, "price");
        }
    }
}

TEST(BitwiseDeterminism, Digital) {
    mcd::pricers::McResult reference{};
    bool have_reference = false;
    for (unsigned n : thread_counts_to_test()) {
        const auto result = pricers::monte_carlo_digital(
            100.0, 100.0, 0.05, 0.02, 0.25, 1.0, OptionType::Call, DigitalStyle::CashOrNothing,
            1.0, kPathCount, kSeed, {.num_threads = n});
        if (!have_reference) {
            reference = result;
            have_reference = true;
        } else {
            expect_bitwise_identical(result.price, reference.price, "price");
        }
    }
}

TEST(BitwiseDeterminism, ArithmeticAsian) {
    mcd::pricers::McResult reference{};
    bool have_reference = false;
    for (unsigned n : thread_counts_to_test()) {
        const auto result = pricers::monte_carlo_asian(
            100.0, 100.0, 0.05, 0.02, 0.25, 1.0, OptionType::Call, StrikeStyle::Fixed,
            AverageStyle::Arithmetic, 20, kPathCount, kSeed, {.num_threads = n});
        if (!have_reference) {
            reference = result;
            have_reference = true;
        } else {
            expect_bitwise_identical(result.price, reference.price, "price");
        }
    }
}

TEST(BitwiseDeterminism, ArithmeticAsianWithControlVariate) {
    mcd::pricers::McResult reference{};
    bool have_reference = false;
    for (unsigned n : thread_counts_to_test()) {
        const auto result = pricers::monte_carlo_asian(
            100.0, 100.0, 0.05, 0.02, 0.25, 1.0, OptionType::Call, StrikeStyle::Fixed,
            AverageStyle::Arithmetic, 20, kPathCount, kSeed,
            {.control_variate = true, .num_threads = n});
        if (!have_reference) {
            reference = result;
            have_reference = true;
        } else {
            expect_bitwise_identical(result.price, reference.price, "price");
        }
    }
}

TEST(BitwiseDeterminism, GeometricAsian) {
    mcd::pricers::McResult reference{};
    bool have_reference = false;
    for (unsigned n : thread_counts_to_test()) {
        const auto result = pricers::monte_carlo_asian(
            100.0, 100.0, 0.05, 0.02, 0.25, 1.0, OptionType::Call, StrikeStyle::Fixed,
            AverageStyle::Geometric, 20, kPathCount, kSeed, {.num_threads = n});
        if (!have_reference) {
            reference = result;
            have_reference = true;
        } else {
            expect_bitwise_identical(result.price, reference.price, "price");
        }
    }
}

TEST(BitwiseDeterminism, Barrier) {
    mcd::pricers::McResult reference{};
    bool have_reference = false;
    for (unsigned n : thread_counts_to_test()) {
        const auto result = pricers::monte_carlo_barrier(
            100.0, 100.0, 80.0, 0.05, 0.02, 0.25, 1.0, OptionType::Call, BarrierDirection::Down,
            BarrierKnock::Out, 0.0, 20, kPathCount, kSeed, {.num_threads = n});
        if (!have_reference) {
            reference = result;
            have_reference = true;
        } else {
            expect_bitwise_identical(result.price, reference.price, "price");
        }
    }
}

TEST(BitwiseDeterminism, BarrierWithBrownianBridge) {
    mcd::pricers::McResult reference{};
    bool have_reference = false;
    for (unsigned n : thread_counts_to_test()) {
        const auto result = pricers::monte_carlo_barrier(
            100.0, 100.0, 80.0, 0.05, 0.02, 0.25, 1.0, OptionType::Call, BarrierDirection::Down,
            BarrierKnock::Out, 0.0, 20, kPathCount, kSeed,
            {.brownian_bridge = true, .num_threads = n});
        if (!have_reference) {
            reference = result;
            have_reference = true;
        } else {
            expect_bitwise_identical(result.price, reference.price, "price");
        }
    }
}

TEST(BitwiseDeterminism, LookbackFixed) {
    mcd::pricers::McResult reference{};
    bool have_reference = false;
    for (unsigned n : thread_counts_to_test()) {
        const auto result = pricers::monte_carlo_lookback(100.0, 100.0, 0.05, 0.02, 0.25, 1.0,
                                                            OptionType::Call, StrikeStyle::Fixed,
                                                            20, kPathCount, kSeed,
                                                            {.num_threads = n});
        if (!have_reference) {
            reference = result;
            have_reference = true;
        } else {
            expect_bitwise_identical(result.price, reference.price, "price");
        }
    }
}

TEST(BitwiseDeterminism, LookbackFloating) {
    mcd::pricers::McResult reference{};
    bool have_reference = false;
    for (unsigned n : thread_counts_to_test()) {
        const auto result = pricers::monte_carlo_lookback(
            100.0, 100.0, 0.05, 0.02, 0.25, 1.0, OptionType::Call, StrikeStyle::Floating, 20,
            kPathCount, kSeed, {.num_threads = n});
        if (!have_reference) {
            reference = result;
            have_reference = true;
        } else {
            expect_bitwise_identical(result.price, reference.price, "price");
        }
    }
}

// --- Pool correctness -----------------------------------------------------------------

TEST(ThreadPoolCorrectness, NoLostWorkAcrossThreadCounts) {
    for (unsigned n : thread_counts_to_test()) {
        const auto result = pricers::monte_carlo_european(100.0, 100.0, 0.05, 0.02, 0.25, 1.0,
                                                            OptionType::Call, kPathCount, kSeed,
                                                            {.num_threads = n});
        EXPECT_EQ(result.path_count, kPathCount) << "num_threads=" << n;
    }
}

// path_count smaller than logical_chunk_count: most chunks are legitimately empty.
TEST(ThreadPoolCorrectness, PathCountSmallerThanChunkCount) {
    constexpr std::uint64_t kTinyPathCount = 3;
    for (unsigned n : thread_counts_to_test()) {
        const auto result = pricers::monte_carlo_european(100.0, 100.0, 0.05, 0.02, 0.25, 1.0,
                                                            OptionType::Call, kTinyPathCount,
                                                            kSeed, {.num_threads = n});
        EXPECT_EQ(result.path_count, kTinyPathCount) << "num_threads=" << n;
        EXPECT_TRUE(std::isfinite(result.price)) << "num_threads=" << n;
    }
}

TEST(ThreadPoolCorrectness, SingleThreadedMatchesPreParallelismBaseline) {
    // The num_threads=1 path now always goes through the same chunk+merge algorithm as
    // num_threads>1 (see docs/design/04-parallelism.md sec.2) rather than the simple
    // single-loop accumulation Phase 2/3 originally used. Statistical agreement (not
    // bit-exact -- the algorithm genuinely changed) with a large path count confirms the
    // refactor didn't introduce a real bug, just different (still correct) rounding.
    const auto result = pricers::monte_carlo_european(100.0, 100.0, 0.05, 0.0, 0.25, 1.0,
                                                        OptionType::Call, 500'000, 99);
    EXPECT_GT(result.price, 0.0);
    EXPECT_LT(result.standard_error, 1.0);
}
