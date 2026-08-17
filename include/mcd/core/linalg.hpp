#pragma once

#include <vector>

namespace mcd {

// Solves the linear least-squares problem min ||A*beta - y||_2 via
// Householder QR (A modified in place; row-major, `rows` x `cols`,
// rows >= cols). Returns beta (size `cols`). Hand-written -- Householder
// reflections, not the normal equations, which square the condition number
// and are numerically poor for the near-collinear polynomial design
// matrices Longstaff-Schwartz regression produces. See
// docs/design/05-greeks-and-american.md sec.3.3.
[[nodiscard]] std::vector<double> householder_least_squares(std::vector<double>& a, int rows,
                                                              int cols,
                                                              std::vector<double> y) noexcept;

} // namespace mcd
