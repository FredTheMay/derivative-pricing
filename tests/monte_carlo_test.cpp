#include "mcd/pricers/analytic.hpp"
#include "mcd/pricers/monte_carlo.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <bit>
#include <cstdint>
#include <cstdlib>
#include <new>

using mcd::OptionType;
namespace pricers = mcd::pricers;

// --- European MC within 3 SE of BSM, >= 20 parameter combinations --------------------

namespace {
struct EuropeanCase {
    double s, k, r, q, sigma, t;
    OptionType type;
};
} // namespace

class MonteCarloEuropeanVsBsm : public ::testing::TestWithParam<EuropeanCase> {};

TEST_P(MonteCarloEuropeanVsBsm, WithinThreeStandardErrors) {
    const auto c = GetParam();
    const auto mc = pricers::monte_carlo_european(c.s, c.k, c.r, c.q, c.sigma, c.t, c.type,
                                                    /*path_count=*/500'000, /*seed=*/123);
    const double analytic = pricers::black_scholes_merton(c.s, c.k, c.r, c.q, c.sigma, c.t, c.type);
    const double deviation = std::abs(mc.price - analytic);
    EXPECT_LT(deviation, 3.0 * mc.standard_error)
        << "MC price=" << mc.price << " analytic=" << analytic << " deviation=" << deviation
        << " SE=" << mc.standard_error << " (deviation/SE=" << deviation / mc.standard_error
        << ")";
}

INSTANTIATE_TEST_SUITE_P(
    ParameterMatrix, MonteCarloEuropeanVsBsm,
    ::testing::Values(
        EuropeanCase{100, 100, 0.05, 0.00, 0.20, 1.0, OptionType::Call},
        EuropeanCase{100, 100, 0.05, 0.00, 0.20, 1.0, OptionType::Put},
        EuropeanCase{100, 80, 0.05, 0.00, 0.20, 1.0, OptionType::Call},
        EuropeanCase{100, 80, 0.05, 0.00, 0.20, 1.0, OptionType::Put},
        EuropeanCase{100, 120, 0.05, 0.00, 0.20, 1.0, OptionType::Call},
        EuropeanCase{100, 120, 0.05, 0.00, 0.20, 1.0, OptionType::Put},
        EuropeanCase{50, 50, 0.03, 0.01, 0.15, 0.5, OptionType::Call},
        EuropeanCase{50, 50, 0.03, 0.01, 0.15, 0.5, OptionType::Put},
        EuropeanCase{150, 140, 0.04, 0.02, 0.35, 2.0, OptionType::Call},
        EuropeanCase{150, 140, 0.04, 0.02, 0.35, 2.0, OptionType::Put},
        EuropeanCase{100, 100, 0.10, 0.00, 0.40, 1.0, OptionType::Call},
        EuropeanCase{100, 100, 0.10, 0.00, 0.40, 1.0, OptionType::Put},
        EuropeanCase{100, 100, 0.01, 0.05, 0.20, 1.0, OptionType::Call},
        EuropeanCase{100, 100, 0.01, 0.05, 0.20, 1.0, OptionType::Put},
        EuropeanCase{100, 100, 0.05, 0.00, 0.20, 0.1, OptionType::Call},
        EuropeanCase{100, 100, 0.05, 0.00, 0.20, 0.1, OptionType::Put},
        EuropeanCase{100, 100, 0.05, 0.00, 0.20, 5.0, OptionType::Call},
        EuropeanCase{100, 100, 0.05, 0.00, 0.20, 5.0, OptionType::Put},
        EuropeanCase{200, 100, 0.06, 0.03, 0.25, 1.5, OptionType::Call},
        EuropeanCase{200, 100, 0.06, 0.03, 0.25, 1.5, OptionType::Put},
        EuropeanCase{80, 120, 0.02, 0.00, 0.30, 1.0, OptionType::Call},
        EuropeanCase{80, 120, 0.02, 0.00, 0.30, 1.0, OptionType::Put}));

// --- Determinism: identical seed => bitwise-identical price -------------------------

TEST(MonteCarloEuropean, DeterministicAcrossRepeatedRuns) {
    const auto run = [] {
        return pricers::monte_carlo_european(100.0, 100.0, 0.05, 0.01, 0.25, 1.0,
                                              OptionType::Call, /*path_count=*/100'000,
                                              /*seed=*/7);
    };
    const auto first = run();
    const auto second = run();
    EXPECT_EQ(std::bit_cast<std::uint64_t>(first.price), std::bit_cast<std::uint64_t>(second.price));
    EXPECT_EQ(std::bit_cast<std::uint64_t>(first.standard_error),
              std::bit_cast<std::uint64_t>(second.standard_error));
}

TEST(MonteCarloEuropean, DifferentSeedsGiveDifferentPrices) {
    const auto a = pricers::monte_carlo_european(100.0, 100.0, 0.05, 0.01, 0.25, 1.0,
                                                   OptionType::Call, 10'000, /*seed=*/1);
    const auto b = pricers::monte_carlo_european(100.0, 100.0, 0.05, 0.01, 0.25, 1.0,
                                                   OptionType::Call, 10'000, /*seed=*/2);
    EXPECT_NE(std::bit_cast<std::uint64_t>(a.price), std::bit_cast<std::uint64_t>(b.price));
}

// --- Zero heap allocation in the pricing loop ----------------------------------------
//
// Overriding global operator new/delete to count allocations is fundamentally
// incompatible with ASan/TSan: their runtimes (libclang_rt.{asan,tsan}_cxx.a) already
// define these same symbols for their own instrumentation, and linking both is a hard
// "multiple definition" error, not a warning -- confirmed for real in CI, where this
// exact conflict failed the tsan (and, by the identical mechanism, asan) build. There
// is no allocator-override technique that coexists with a sanitizer that itself
// intercepts the allocator. Skipped (not silently omitted -- reported as SKIPPED, with
// the reason, in every sanitizer build's test output) under those two configurations
// only; debug, release, and ubsan all still run the real check.
#if defined(__SANITIZE_ADDRESS__) || defined(__SANITIZE_THREAD__)
#define MCD_SKIP_ALLOCATOR_OVERRIDE 1
#elif defined(__has_feature)
#if __has_feature(address_sanitizer) || __has_feature(thread_sanitizer)
#define MCD_SKIP_ALLOCATOR_OVERRIDE 1
#endif
#endif
#ifndef MCD_SKIP_ALLOCATOR_OVERRIDE
#define MCD_SKIP_ALLOCATOR_OVERRIDE 0
#endif

#if !MCD_SKIP_ALLOCATOR_OVERRIDE
namespace {
std::atomic<std::uint64_t> g_new_count{0};
} // namespace

void* operator new(std::size_t size) {
    ++g_new_count;
    if (void* p = std::malloc(size)) {
        return p;
    }
    throw std::bad_alloc();
}

void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
#endif // !MCD_SKIP_ALLOCATOR_OVERRIDE

TEST(MonteCarloEuropean, ZeroHeapAllocationsInPricingLoop) {
#if MCD_SKIP_ALLOCATOR_OVERRIDE
    GTEST_SKIP() << "operator new/delete override is incompatible with the sanitizer "
                    "runtime's own allocator interception (see comment above); zero-"
                    "allocation is verified under debug/release/ubsan instead.";
#else
    // Warm up anything GoogleTest itself might lazily allocate before we start counting.
    (void)pricers::monte_carlo_european(100.0, 100.0, 0.05, 0.0, 0.2, 1.0, OptionType::Call, 10,
                                         1);

    const std::uint64_t before = g_new_count.load();
    const auto result = pricers::monte_carlo_european(100.0, 100.0, 0.05, 0.0, 0.2, 1.0,
                                                        OptionType::Call, /*path_count=*/1'000'000,
                                                        /*seed=*/5);
    const std::uint64_t after = g_new_count.load();

    EXPECT_EQ(before, after) << "operator new was called " << (after - before)
                              << " times during 1e6-path pricing";
    EXPECT_GT(result.path_count, 0u);
#endif // MCD_SKIP_ALLOCATOR_OVERRIDE
}
