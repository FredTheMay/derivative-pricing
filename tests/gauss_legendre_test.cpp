#include "mcd/core/gauss_legendre.hpp"

#include <gtest/gtest.h>

#include <cmath>

// Independent, self-contained correctness check (CLAUDE.md sec.2.5): an order-n
// Gauss-Legendre rule must integrate every polynomial of degree <= 2n-1 exactly.
// This does not depend on Heston, the characteristic function, or anything else this
// quadrature will later be used for -- it validates the quadrature machinery itself,
// against the exact closed form int_{-1}^{1} x^k dx = 0 (k odd) or 2/(k+1) (k even).
TEST(GaussLegendre, ExactForPolynomialsUpToDegree2NMinus1) {
    for (int order : {2, 4, 8, 16, 32, 64}) {
        for (int k = 0; k <= 2 * order - 1; ++k) {
            const double integral = mcd::gauss_legendre_integrate(
                [k](double x) { return std::pow(x, k); }, -1.0, 1.0, order);
            const double exact = (k % 2 == 0) ? 2.0 / static_cast<double>(k + 1) : 0.0;
            EXPECT_NEAR(integral, exact, 1e-10)
                << "order=" << order << " k=" << k;
        }
    }
}

TEST(GaussLegendre, WeightsArePositiveAndSumToIntervalWidth) {
    for (int order : {2, 8, 32, 64}) {
        const auto rule = mcd::gauss_legendre_rule(order);
        double sum = 0.0;
        for (double w : rule.weights) {
            EXPECT_GT(w, 0.0);
            sum += w;
        }
        EXPECT_NEAR(sum, 2.0, 1e-10); // width of [-1, 1]
    }
}

TEST(GaussLegendre, IntegratesOverArbitraryInterval) {
    // int_0^2 x^3 dx = 4 (exact), well within an order-4 rule's exactness degree (7).
    const double integral =
        mcd::gauss_legendre_integrate([](double x) { return x * x * x; }, 0.0, 2.0, 4);
    EXPECT_NEAR(integral, 4.0, 1e-10);
}

TEST(GaussLegendre, IntegratesKnownTranscendentalIntegral) {
    // int_0^{pi/2} sin(x) dx = 1 -- not a polynomial, so this checks the quadrature
    // behaves sensibly (converges) beyond its exact-degree guarantee, at a high
    // enough order that the residual is negligible.
    const double integral = mcd::gauss_legendre_integrate(
        [](double x) { return std::sin(x); }, 0.0, std::numbers::pi / 2.0, 64);
    EXPECT_NEAR(integral, 1.0, 1e-12);
}
