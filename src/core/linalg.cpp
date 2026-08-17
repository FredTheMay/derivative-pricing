#include "mcd/core/linalg.hpp"

#include <cmath>

namespace mcd {

std::vector<double> householder_least_squares(std::vector<double>& a, int rows, int cols,
                                                std::vector<double> y) noexcept {
    for (int k = 0; k < cols; ++k) {
        double norm_sq = 0.0;
        for (int i = k; i < rows; ++i) {
            const double v = a[static_cast<std::size_t>(i * cols + k)];
            norm_sq += v * v;
        }
        const double norm = std::sqrt(norm_sq);
        if (norm == 0.0) {
            continue; // degenerate column (e.g. all zero); leave as-is
        }

        const double akk = a[static_cast<std::size_t>(k * cols + k)];
        // Sign chosen opposite akk to avoid catastrophic cancellation in v[0].
        const double alpha = akk >= 0.0 ? -norm : norm;

        std::vector<double> v(static_cast<std::size_t>(rows - k));
        v[0] = akk - alpha;
        for (int i = k + 1; i < rows; ++i) {
            v[static_cast<std::size_t>(i - k)] = a[static_cast<std::size_t>(i * cols + k)];
        }
        double v_norm_sq = 0.0;
        for (double vi : v) {
            v_norm_sq += vi * vi;
        }
        if (v_norm_sq == 0.0) {
            continue;
        }

        // Apply the reflection H = I - 2*v*v^T/(v^T*v) to the trailing columns of A...
        for (int j = k; j < cols; ++j) {
            double dot = 0.0;
            for (int i = k; i < rows; ++i) {
                dot += v[static_cast<std::size_t>(i - k)] * a[static_cast<std::size_t>(i * cols + j)];
            }
            const double factor = 2.0 * dot / v_norm_sq;
            for (int i = k; i < rows; ++i) {
                a[static_cast<std::size_t>(i * cols + j)] -=
                    factor * v[static_cast<std::size_t>(i - k)];
            }
        }
        // ...and to y, accumulating Q^T*y without ever forming Q explicitly.
        double dot_y = 0.0;
        for (int i = k; i < rows; ++i) {
            dot_y += v[static_cast<std::size_t>(i - k)] * y[static_cast<std::size_t>(i)];
        }
        const double factor_y = 2.0 * dot_y / v_norm_sq;
        for (int i = k; i < rows; ++i) {
            y[static_cast<std::size_t>(i)] -= factor_y * v[static_cast<std::size_t>(i - k)];
        }
    }

    // Back-substitution: R (upper triangular, top cols x cols block of a) * beta = y[0:cols].
    std::vector<double> beta(static_cast<std::size_t>(cols), 0.0);
    for (int i = cols - 1; i >= 0; --i) {
        double sum = y[static_cast<std::size_t>(i)];
        for (int j = i + 1; j < cols; ++j) {
            sum -= a[static_cast<std::size_t>(i * cols + j)] * beta[static_cast<std::size_t>(j)];
        }
        const double diag = a[static_cast<std::size_t>(i * cols + i)];
        beta[static_cast<std::size_t>(i)] = diag != 0.0 ? sum / diag : 0.0;
    }
    return beta;
}

} // namespace mcd
