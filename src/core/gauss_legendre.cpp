#include "mcd/core/gauss_legendre.hpp"

#include <cmath>
#include <numbers>

namespace mcd {

namespace {

// P_n(x) and P_{n-1}(x) via the standard three-term recurrence
// n*P_n(x) = (2n-1)*x*P_{n-1}(x) - (n-1)*P_{n-2}(x), P_0=1, P_1=x.
struct LegendreValue {
    double p_n;
    double p_n_minus_1;
};

LegendreValue legendre(int n, double x) noexcept {
    double p_prev = 1.0; // P_0
    double p_curr = x;   // P_1
    if (n == 0) {
        return {.p_n = 1.0, .p_n_minus_1 = 0.0};
    }
    for (int k = 2; k <= n; ++k) {
        const double p_next =
            ((2.0 * static_cast<double>(k) - 1.0) * x * p_curr -
             (static_cast<double>(k) - 1.0) * p_prev) /
            static_cast<double>(k);
        p_prev = p_curr;
        p_curr = p_next;
    }
    return {.p_n = p_curr, .p_n_minus_1 = p_prev};
}

} // namespace

GaussLegendreRule gauss_legendre_rule(int order) {
    GaussLegendreRule rule;
    rule.nodes.resize(static_cast<std::size_t>(order));
    rule.weights.resize(static_cast<std::size_t>(order));

    const int half = (order + 1) / 2; // exploit symmetry: roots come in +-pairs
    for (int i = 0; i < half; ++i) {
        // Standard initial guess (Numerical Recipes' gauleg), refined by Newton's
        // method below to full double precision.
        double x = std::cos(std::numbers::pi * (static_cast<double>(i) + 0.75) /
                             (static_cast<double>(order) + 0.5));

        double p_prime = 0.0;
        for (int iter = 0; iter < 100; ++iter) {
            const LegendreValue leg = legendre(order, x);
            p_prime = static_cast<double>(order) * (x * leg.p_n - leg.p_n_minus_1) /
                      (x * x - 1.0);
            const double dx = leg.p_n / p_prime;
            x -= dx;
            if (std::abs(dx) < 1e-15) {
                break;
            }
        }

        const double weight = 2.0 / ((1.0 - x * x) * p_prime * p_prime);
        const auto lo = static_cast<std::size_t>(i);
        const auto hi = static_cast<std::size_t>(order - 1 - i);
        rule.nodes[lo] = -x;
        rule.nodes[hi] = x;
        rule.weights[lo] = weight;
        rule.weights[hi] = weight;
    }
    return rule;
}

} // namespace mcd
