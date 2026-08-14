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

} // namespace mcd
