#pragma once

#include <vector>

namespace mcd {

// Gauss-Legendre quadrature nodes/weights, computed from scratch via Newton-Raphson
// root-finding on Legendre polynomials -- not a transcribed table (Stretch Goal 5,
// docs/design/12-heston.md sec.4: same "verify, don't transcribe" discipline Stretch
// Goal 3 applied to Sobol's direction numbers, per CLAUDE.md sec.2.5). Exact for
// polynomials up to degree 2*order - 1 on [-1, 1]; tests/gauss_legendre_test.cpp
// checks exactly that, independent of any downstream use.
struct GaussLegendreRule {
    std::vector<double> nodes;   // on [-1, 1], ascending
    std::vector<double> weights; // matching nodes, all positive
};

[[nodiscard]] GaussLegendreRule gauss_legendre_rule(int order);

// Integrates f over [a, b] using an `order`-point rule, via the standard affine
// mapping from [-1, 1]. f must be callable as double(double).
template <typename F>
[[nodiscard]] double gauss_legendre_integrate(const F& f, double a, double b, int order) {
    const GaussLegendreRule rule = gauss_legendre_rule(order);
    const double half_width = 0.5 * (b - a);
    const double midpoint = 0.5 * (a + b);
    double sum = 0.0;
    for (std::size_t i = 0; i < rule.nodes.size(); ++i) {
        const double x = midpoint + half_width * rule.nodes[i];
        sum += rule.weights[i] * f(x);
    }
    return half_width * sum;
}

} // namespace mcd
