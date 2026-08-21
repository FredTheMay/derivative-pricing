#include "mcd/core/rng_simd.hpp"

#include "mcd/core/rng.hpp"

#include <gtest/gtest.h>

#include <bit>
#include <cstdint>
#include <vector>

namespace {

// std::bit_cast-based comparison, not EXPECT_DOUBLE_EQ -- bitwise identity is the
// actual requirement (docs/design/11-simd.md sec.4/5), not "close."
void expect_bitwise_equal(double a, double b, const char* where) {
    EXPECT_EQ(std::bit_cast<std::uint64_t>(a), std::bit_cast<std::uint64_t>(b)) << where;
}

} // namespace

TEST(RngSimd, ReportsNeonAvailability) {
    // This is a compile-time constant; the test just documents that it's reachable
    // and prints which path the rest of this suite is actually exercising.
    if (mcd::kHasNeon) {
        SUCCEED() << "NEON path active";
    } else {
        SUCCEED() << "scalar fallback active (non-ARM build)";
    }
}

TEST(RngSimd, Batch4MatchesFourScalarCallsBitwise) {
    const std::vector<std::uint64_t> seeds = {0, 1, 2024, 0xDEADBEEFULL};
    const std::vector<std::uint64_t> bases = {0, 1, 1000, 999997, 4294967292ULL /* 2^32-4 */};
    const std::vector<std::uint32_t> draw_indices = {0, 1, 5, 100};

    for (std::uint64_t seed : seeds) {
        for (std::uint64_t base : bases) {
            for (std::uint32_t draw : draw_indices) {
                const auto batch = mcd::standard_normal_variate_batch4(seed, base, draw);
                for (int i = 0; i < 4; ++i) {
                    const double scalar =
                        mcd::standard_normal_variate(seed, base + static_cast<std::uint64_t>(i), draw);
                    expect_bitwise_equal(batch[static_cast<std::size_t>(i)], scalar,
                                          "seed/base/draw/lane mismatch");
                }
            }
        }
    }
}

TEST(RngSimd, Batch4AcrossCounterWordBoundaryMatchesScalar) {
    // path_index_base straddling the 2^32 boundary within one batch of 4 -- word 0
    // wraps and must carry into word 1 for at least one lane in the batch.
    const std::uint64_t base = (std::uint64_t{1} << 32) - 2; // lanes: ...FFFFFFFE, FFFFFFFF, 1_00000000, 1_00000001
    const auto batch = mcd::standard_normal_variate_batch4(/*seed=*/42, base, /*draw_index=*/3);
    for (int i = 0; i < 4; ++i) {
        const double scalar =
            mcd::standard_normal_variate(42, base + static_cast<std::uint64_t>(i), 3);
        expect_bitwise_equal(batch[static_cast<std::size_t>(i)], scalar, "counter-boundary mismatch");
    }
}

TEST(RngSimd, DeterministicAcrossRepeatedCalls) {
    const auto a = mcd::standard_normal_variate_batch4(7, 12345, 2);
    const auto b = mcd::standard_normal_variate_batch4(7, 12345, 2);
    for (int i = 0; i < 4; ++i) {
        expect_bitwise_equal(a[static_cast<std::size_t>(i)], b[static_cast<std::size_t>(i)],
                              "repeated call mismatch");
    }
}
