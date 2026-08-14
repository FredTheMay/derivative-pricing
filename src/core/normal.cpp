#include "mcd/core/normal.hpp"

#include <cmath>
#include <numbers>

namespace mcd {

double standard_normal_pdf(double x) noexcept {
    constexpr double inv_sqrt_2pi = std::numbers::inv_sqrtpi / std::numbers::sqrt2;
    return inv_sqrt_2pi * std::exp(-0.5 * x * x);
}

double standard_normal_cdf(double x) noexcept {
    return 0.5 * std::erfc(-x / std::numbers::sqrt2);
}

namespace {

// Acklam's coefficients (Peter J. Acklam), as verified against QuantLib's
// InverseCumulativeNormal (ql/math/distributions/normaldistribution.cpp),
// itself documented there as implementing Acklam's published algorithm.
constexpr double kA1 = -3.969683028665376e+01;
constexpr double kA2 = 2.209460984245205e+02;
constexpr double kA3 = -2.759285104469687e+02;
constexpr double kA4 = 1.383577518672690e+02;
constexpr double kA5 = -3.066479806614716e+01;
constexpr double kA6 = 2.506628277459239e+00;

constexpr double kB1 = -5.447609879822406e+01;
constexpr double kB2 = 1.615858368580409e+02;
constexpr double kB3 = -1.556989798598866e+02;
constexpr double kB4 = 6.680131188771972e+01;
constexpr double kB5 = -1.328068155288572e+01;

constexpr double kC1 = -7.784894002430293e-03;
constexpr double kC2 = -3.223964580411365e-01;
constexpr double kC3 = -2.400758277161838e+00;
constexpr double kC4 = -2.549732539343734e+00;
constexpr double kC5 = 4.374664141464968e+00;
constexpr double kC6 = 2.938163982698783e+00;

constexpr double kD1 = 7.784695709041462e-03;
constexpr double kD2 = 3.224671290700398e-01;
constexpr double kD3 = 2.445134137142996e+00;
constexpr double kD4 = 3.754408661907416e+00;

constexpr double kLowBreak = 0.02425;
constexpr double kHighBreak = 1.0 - kLowBreak;

double acklam_tail(double u) noexcept {
    const bool lower = u < kLowBreak;
    const double z = std::sqrt(-2.0 * std::log(lower ? u : 1.0 - u));
    const double numerator = (((((kC1 * z + kC2) * z + kC3) * z + kC4) * z + kC5) * z + kC6);
    const double denominator = ((((kD1 * z + kD2) * z + kD3) * z + kD4) * z + 1.0);
    const double result = numerator / denominator;
    return lower ? result : -result;
}

double acklam_central(double u) noexcept {
    const double z = u - 0.5;
    const double r = z * z;
    const double numerator =
        (((((kA1 * r + kA2) * r + kA3) * r + kA4) * r + kA5) * r + kA6) * z;
    const double denominator = (((((kB1 * r + kB2) * r + kB3) * r + kB4) * r + kB5) * r + 1.0);
    return numerator / denominator;
}

} // namespace

double inverse_standard_normal_cdf(double u) noexcept {
    double z = (u < kLowBreak || kHighBreak < u) ? acklam_tail(u) : acklam_central(u);

    // One Halley (third-order) refinement step against the erfc-based CDF:
    // error = Phi(z) - u, divided by the density Phi'(z), with a second-order
    // (Halley) correction term.
    const double error = standard_normal_cdf(z) - u;
    const double sqrt_2pi = std::sqrt(2.0 * std::numbers::pi);
    const double r = error * sqrt_2pi * std::exp(0.5 * z * z);
    z -= r / (1.0 + 0.5 * z * r);

    return z;
}

} // namespace mcd
