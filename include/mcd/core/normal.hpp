#pragma once

namespace mcd {

// Standard normal density phi(x) = (1/sqrt(2*pi)) * exp(-x^2/2).
[[nodiscard]] double standard_normal_pdf(double x) noexcept;

// Standard normal CDF Phi(x), via std::erf.
[[nodiscard]] double standard_normal_cdf(double x) noexcept;

} // namespace mcd
