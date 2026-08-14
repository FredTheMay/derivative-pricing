#pragma once

namespace mcd {

// Standard normal density phi(x) = (1/sqrt(2*pi)) * exp(-x^2/2).
[[nodiscard]] double standard_normal_pdf(double x) noexcept;

// Standard normal CDF Phi(x), via std::erf.
[[nodiscard]] double standard_normal_cdf(double x) noexcept;

// Inverse standard normal CDF Phi^-1(u) for u in (0,1): Acklam's rational
// approximation (relative error < 1.15e-9), refined by one Halley step to
// full machine precision.
[[nodiscard]] double inverse_standard_normal_cdf(double u) noexcept;

} // namespace mcd
