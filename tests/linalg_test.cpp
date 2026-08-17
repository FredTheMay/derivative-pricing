#include "mcd/core/linalg.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <vector>

namespace {

// Forms and solves the normal equations (A^T A) beta = A^T y directly -- the approach
// CLAUDE.md rejects in favor of Householder QR, because forming A^T A squares the
// condition number. Implemented here, test-only, purely as a comparison baseline (reuses
// householder_least_squares on the resulting square system, isolating exactly the "does
// explicitly forming A^T A hurt" effect from any other implementation difference).
std::vector<double> normal_equations_solve(const std::vector<double>& a, int rows, int cols,
                                            const std::vector<double>& y) {
    std::vector<double> ata(static_cast<std::size_t>(cols * cols), 0.0);
    std::vector<double> aty(static_cast<std::size_t>(cols), 0.0);
    for (int i = 0; i < cols; ++i) {
        for (int j = 0; j < cols; ++j) {
            double sum = 0.0;
            for (int k = 0; k < rows; ++k) {
                sum += a[static_cast<std::size_t>(k * cols + i)] *
                       a[static_cast<std::size_t>(k * cols + j)];
            }
            ata[static_cast<std::size_t>(i * cols + j)] = sum;
        }
        double sum_y = 0.0;
        for (int k = 0; k < rows; ++k) {
            sum_y += a[static_cast<std::size_t>(k * cols + i)] * y[static_cast<std::size_t>(k)];
        }
        aty[static_cast<std::size_t>(i)] = sum_y;
    }
    return mcd::householder_least_squares(ata, cols, cols, aty);
}

} // namespace

TEST(HouseholderLeastSquares, MatchesHandComputedOverdeterminedSystem) {
    // Classic textbook line fit: y = b0 + b1*x through (1,1),(2,2),(3,2).
    std::vector<double> a = {1, 1, 1, 2, 1, 3};
    std::vector<double> y = {1, 2, 2};
    const auto beta = mcd::householder_least_squares(a, 3, 2, y);
    EXPECT_NEAR(beta[0], 2.0 / 3.0, 1e-9);
    EXPECT_NEAR(beta[1], 0.5, 1e-9);
}

TEST(HouseholderLeastSquares, SolvesSquareSystemExactly) {
    std::vector<double> a = {2, 1, 1, 1, 3, 2, 1, 0, 0};
    std::vector<double> y = {4, 5, 6};
    const auto beta = mcd::householder_least_squares(a, 3, 3, y);
    EXPECT_NEAR(beta[0], 6.0, 1e-9);
    EXPECT_NEAR(beta[1], 15.0, 1e-9);
    EXPECT_NEAR(beta[2], -23.0, 1e-9);
}

TEST(HouseholderLeastSquares, AgreesWithNormalEquationsOnWellConditionedSystem) {
    std::vector<double> a = {1, 0, 0, 1, 1, 1, 1, 2, 4, 1, 3, 9};
    std::vector<double> y = {1, 2, 5, 10};
    std::vector<double> a_copy = a;
    const auto beta_qr = mcd::householder_least_squares(a, 4, 3, y);
    const auto beta_ne = normal_equations_solve(a_copy, 4, 3, y);
    for (int i = 0; i < 3; ++i) {
        EXPECT_NEAR(beta_qr[static_cast<std::size_t>(i)], beta_ne[static_cast<std::size_t>(i)],
                    1e-6);
    }
}

// Ill-conditioned design matrix: a cubic Vandermonde system with x-values clustered
// extremely close together. Recovering a *known* beta from noiseless y = A*beta_true
// isolates exactly the numerical stability difference CLAUDE.md's design decision is
// about: normal equations square the condition number of an already poorly-conditioned
// matrix, Householder QR does not.
TEST(HouseholderLeastSquares, MoreAccurateThanNormalEquationsOnIllConditionedSystem) {
    const std::vector<double> xs = {1.0, 1.0001, 1.0002, 1.0003};
    const std::vector<double> beta_true = {2.0, -3.0, 5.0, -1.0}; // 1, x, x^2, x^3

    std::vector<double> a;
    std::vector<double> y;
    a.reserve(xs.size() * 4);
    y.reserve(xs.size());
    for (double x : xs) {
        const double row[4] = {1.0, x, x * x, x * x * x};
        for (double v : row) {
            a.push_back(v);
        }
        double yi = 0.0;
        for (int j = 0; j < 4; ++j) {
            yi += row[j] * beta_true[static_cast<std::size_t>(j)];
        }
        y.push_back(yi);
    }

    std::vector<double> a_copy = a;
    const auto beta_qr =
        mcd::householder_least_squares(a, static_cast<int>(xs.size()), 4, y);
    const auto beta_ne =
        normal_equations_solve(a_copy, static_cast<int>(xs.size()), 4, y);

    double qr_error = 0.0, ne_error = 0.0;
    for (int i = 0; i < 4; ++i) {
        qr_error += std::abs(beta_qr[static_cast<std::size_t>(i)] -
                              beta_true[static_cast<std::size_t>(i)]);
        ne_error += std::abs(beta_ne[static_cast<std::size_t>(i)] -
                              beta_true[static_cast<std::size_t>(i)]);
    }
    EXPECT_LT(qr_error, ne_error)
        << "Householder QR error=" << qr_error << " normal-equations error=" << ne_error
        << " (expected QR to recover the known coefficients more accurately)";
}
