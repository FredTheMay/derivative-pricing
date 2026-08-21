#include "mcd/core/sobol.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

// Two independent checks, per docs/design/10-sobol-qmc.md sec.2/7 -- neither relies on
// an external Sobol reference table, since the whole point of this implementation is to
// use only what can be verified from first principles in-session (CLAUDE.md sec.2.5).

// Check 1: every polynomial mcd::sobol_point's direction numbers are built from
// (src/core/sobol.cpp's kPolynomials) is genuinely primitive over GF(2), confirmed by
// direct Fibonacci-LFSR simulation: a degree-s polynomial is primitive iff its LFSR,
// started from any nonzero seed, has period exactly 2^s - 1 (maximal length). This test
// re-derives that fact independently of sobol.cpp's internals -- it does not call
// mcd::sobol_point at all -- so it stands as an outside check on the polynomial choice,
// not a tautology against the implementation under test.
TEST(Sobol, PrimitivePolynomialsHaveMaximalLfsrPeriod) {
    struct Poly {
        unsigned degree;
        std::vector<unsigned> taps; // feedback taps for c(x) = x^degree + ... + 1
        const char* name;
    };
    // Matches the polynomials documented in src/core/sobol.cpp / docs/design/10-sobol-qmc.md
    // sec.2: x+1, x^2+x+1, x^3+x+1, x^4+x+1, x^5+x^2+1, x^6+x+1.
    const std::vector<Poly> polys = {
        {1, {0}, "x+1"},           {2, {0, 1}, "x^2+x+1"},   {3, {0, 1}, "x^3+x+1"},
        {4, {0, 1}, "x^4+x+1"},    {5, {0, 2}, "x^5+x^2+1"}, {6, {0, 1}, "x^6+x+1"},
    };

    for (const auto& poly : polys) {
        std::vector<int> state(poly.degree, 0);
        state[0] = 1; // any nonzero seed
        const std::vector<int> initial = state;

        std::uint64_t period = 0;
        const std::uint64_t max_iterations = (std::uint64_t{1} << poly.degree) + 1;
        do {
            int new_bit = 0;
            for (unsigned t : poly.taps) {
                new_bit ^= state[t];
            }
            for (unsigned i = 0; i + 1 < poly.degree; ++i) {
                state[i] = state[i + 1];
            }
            state[poly.degree - 1] = new_bit;
            ++period;
        } while (state != initial && period < max_iterations);

        const std::uint64_t expected_period = (std::uint64_t{1} << poly.degree) - 1;
        EXPECT_EQ(period, expected_period) << poly.name << " (degree " << poly.degree << ")";
    }
}

// Check 2: 1-D stratification, the defining property of a base-2 (0,m,1)-net and the
// actual testable claim behind "low-discrepancy" (docs/design/10-sobol-qmc.md sec.2) --
// among the first 2^k points of any single dimension, each of the 2^k equal-width
// sub-intervals of [0,1) contains exactly one point. This is checked directly against
// mcd::sobol_point, unlike Check 1 above.
TEST(Sobol, OneDimensionalStratification) {
    for (unsigned dimension = 0; dimension < mcd::kSobolMaxDimensions; ++dimension) {
        for (unsigned k : {1u, 2u, 4u, 6u}) {
            const unsigned n = 1u << k;
            std::vector<bool> hit(n, false);
            for (unsigned index = 0; index < n; ++index) {
                const double x = mcd::sobol_point(dimension, index);
                ASSERT_GE(x, 0.0);
                ASSERT_LT(x, 1.0);
                const unsigned bin = static_cast<unsigned>(x * static_cast<double>(n));
                ASSERT_FALSE(hit[bin]) << "dimension=" << dimension << " k=" << k
                                        << " bin=" << bin << " hit twice";
                hit[bin] = true;
            }
            for (unsigned bin = 0; bin < n; ++bin) {
                ASSERT_TRUE(hit[bin]) << "dimension=" << dimension << " k=" << k << " bin=" << bin
                                       << " never hit";
            }
        }
    }
}

TEST(Sobol, FirstPointIsZeroByConvention) {
    for (unsigned dimension = 0; dimension < mcd::kSobolMaxDimensions; ++dimension) {
        EXPECT_EQ(mcd::sobol_point(dimension, 0), 0.0);
    }
}

TEST(Sobol, DeterministicGivenDimensionAndIndex) {
    for (unsigned dimension = 0; dimension < mcd::kSobolMaxDimensions; ++dimension) {
        for (std::uint64_t index : {1ULL, 7ULL, 1000ULL, 1'000'000ULL}) {
            EXPECT_EQ(mcd::sobol_point(dimension, index), mcd::sobol_point(dimension, index));
        }
    }
}
