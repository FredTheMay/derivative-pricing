#include "mcd/pricers/lsm.hpp"

#include "mcd/core/linalg.hpp"
#include "mcd/core/rng.hpp"
#include "mcd/core/stats.hpp"
#include "mcd/models/gbm.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace mcd::pricers {

namespace {

constexpr int kLaguerreDegree = 3;
constexpr int kBasisSize = kLaguerreDegree + 1;

// The original Longstaff-Schwartz weighted Laguerre basis, evaluated on x = S/K
// (normalized price). See docs/design/05-greeks-and-american.md sec.3.2.
[[nodiscard]] std::array<double, kBasisSize> laguerre_basis(double x) noexcept {
    const double e = std::exp(-x / 2.0);
    return {e, e * (1.0 - x), e * (1.0 - 2.0 * x + 0.5 * x * x),
            e * (1.0 - 3.0 * x + 1.5 * x * x - x * x * x / 6.0)};
}

[[nodiscard]] double intrinsic(double spot, double strike, OptionType type) noexcept {
    const double phi = type == OptionType::Call ? 1.0 : -1.0;
    return std::max(phi * (spot - strike), 0.0);
}

// Forward-simulates every path's price at every monitoring date into a flat,
// row-major (path_count x (monitoring_points+1)) buffer -- the one heap allocation LSM
// needs (CLAUDE.md's documented streaming exception), not per-path or per-step.
[[nodiscard]] std::vector<double> simulate_paths(double spot, double rate, double carry_yield,
                                                  double vol, double time, int monitoring_points,
                                                  std::uint64_t path_count,
                                                  std::uint64_t seed) noexcept {
    const double dt = time / static_cast<double>(monitoring_points);
    const auto stride = static_cast<std::size_t>(monitoring_points) + 1;
    std::vector<double> paths(static_cast<std::size_t>(path_count) * stride);

    for (std::uint64_t path_index = 0; path_index < path_count; ++path_index) {
        double s = spot;
        const std::size_t row = static_cast<std::size_t>(path_index) * stride;
        paths[row] = s;
        for (int step = 0; step < monitoring_points; ++step) {
            const double z =
                standard_normal_variate(seed, path_index, static_cast<std::uint32_t>(step));
            const models::GbmParams step_params{.spot = s, .rate = rate,
                                                  .carry_yield = carry_yield, .vol = vol,
                                                  .time = dt};
            s = models::gbm_terminal_spot(step_params, z);
            paths[row + static_cast<std::size_t>(step) + 1] = s;
        }
    }
    return paths;
}

} // namespace

LsmResult monte_carlo_lsm_american(double spot, double strike, double rate, double carry_yield,
                                    double vol, double time, OptionType type,
                                    int monitoring_points, std::uint64_t path_count,
                                    std::uint64_t seed) noexcept {
    const double dt = time / static_cast<double>(monitoring_points);
    const double disc = std::exp(-rate * dt);
    const auto stride = static_cast<std::size_t>(monitoring_points) + 1;

    const std::vector<double> paths =
        simulate_paths(spot, rate, carry_yield, vol, time, monitoring_points, path_count, seed);

    LsmPolicy policy;
    policy.coefficients_by_date.resize(stride);

    std::vector<double> cashflow(path_count);
    for (std::uint64_t i = 0; i < path_count; ++i) {
        const double terminal =
            paths[static_cast<std::size_t>(i) * stride + static_cast<std::size_t>(monitoring_points)];
        cashflow[i] = intrinsic(terminal, strike, type);
    }

    for (int step = monitoring_points - 1; step >= 1; --step) {
        for (auto& cf : cashflow) {
            cf *= disc;
        }

        std::vector<double> design;
        std::vector<double> targets;
        std::vector<std::uint64_t> itm_indices;
        for (std::uint64_t i = 0; i < path_count; ++i) {
            const double s = paths[static_cast<std::size_t>(i) * stride +
                                    static_cast<std::size_t>(step)];
            if (intrinsic(s, strike, type) > 0.0) {
                const auto basis = laguerre_basis(s / strike);
                design.insert(design.end(), basis.begin(), basis.end());
                targets.push_back(cashflow[i]);
                itm_indices.push_back(i);
            }
        }

        if (itm_indices.size() < static_cast<std::size_t>(kBasisSize)) {
            continue; // too few ITM paths to regress; no exercise decisions at this date
        }

        const auto coefficients = householder_least_squares(
            design, static_cast<int>(itm_indices.size()), kBasisSize, targets);
        policy.coefficients_by_date[static_cast<std::size_t>(step)] = coefficients;

        for (std::uint64_t i : itm_indices) {
            const double s = paths[static_cast<std::size_t>(i) * stride +
                                    static_cast<std::size_t>(step)];
            const auto basis = laguerre_basis(s / strike);
            double continuation = 0.0;
            for (int b = 0; b < kBasisSize; ++b) {
                continuation += coefficients[static_cast<std::size_t>(b)] * basis[static_cast<std::size_t>(b)];
            }
            const double exercise_value = intrinsic(s, strike, type);
            if (exercise_value > continuation) {
                cashflow[i] = exercise_value;
            }
        }
    }

    for (auto& cf : cashflow) {
        cf *= disc;
    }

    WelfordAccumulator acc;
    for (double cf : cashflow) {
        acc.add(cf);
    }

    // Inception (t=0) decision: unlike every other exercise date, this is a single
    // deterministic scalar comparison, not a per-path regression -- spot at t=0 is one
    // number, shared by every path, so no Monte Carlo noise attaches to this decision. If
    // immediate exercise beats the regression-estimated continuation value, the price *is*
    // the intrinsic value exactly (standard_error = 0 for that component), which is what
    // makes "American put >= immediate exercise value everywhere" hold by construction
    // rather than merely with high probability. See docs/validation-report.md Phase 5.
    const double inception_intrinsic = intrinsic(spot, strike, type);
    if (inception_intrinsic > acc.mean()) {
        policy.exercise_at_inception = true;
        return LsmResult{.price = inception_intrinsic, .standard_error = 0.0,
                          .policy = std::move(policy)};
    }
    return LsmResult{.price = acc.mean(), .standard_error = acc.standard_error(),
                      .policy = std::move(policy)};
}

double reprice_against_frozen_policy(double spot, double strike, double rate, double carry_yield,
                                      double vol, double time, OptionType type,
                                      int monitoring_points, std::uint64_t path_count,
                                      std::uint64_t seed, const LsmPolicy& policy) noexcept {
    const double dt = time / static_cast<double>(monitoring_points);
    const double disc = std::exp(-rate * dt);
    const auto stride = static_cast<std::size_t>(monitoring_points) + 1;

    const std::vector<double> paths =
        simulate_paths(spot, rate, carry_yield, vol, time, monitoring_points, path_count, seed);

    std::vector<double> cashflow(path_count);
    for (std::uint64_t i = 0; i < path_count; ++i) {
        const double terminal =
            paths[static_cast<std::size_t>(i) * stride + static_cast<std::size_t>(monitoring_points)];
        cashflow[i] = intrinsic(terminal, strike, type);
    }

    for (int step = monitoring_points - 1; step >= 1; --step) {
        for (auto& cf : cashflow) {
            cf *= disc;
        }

        const auto& coefficients = policy.coefficients_by_date[static_cast<std::size_t>(step)];
        if (coefficients.empty()) {
            continue; // base run made no exercise decisions at this date either
        }

        for (std::uint64_t i = 0; i < path_count; ++i) {
            const double s = paths[static_cast<std::size_t>(i) * stride +
                                    static_cast<std::size_t>(step)];
            const double exercise_value = intrinsic(s, strike, type);
            if (exercise_value <= 0.0) {
                continue;
            }
            const auto basis = laguerre_basis(s / strike);
            double continuation = 0.0;
            for (int b = 0; b < kBasisSize; ++b) {
                continuation += coefficients[static_cast<std::size_t>(b)] * basis[static_cast<std::size_t>(b)];
            }
            if (exercise_value > continuation) {
                cashflow[i] = exercise_value;
            }
        }
    }

    for (auto& cf : cashflow) {
        cf *= disc;
    }

    WelfordAccumulator acc;
    for (double cf : cashflow) {
        acc.add(cf);
    }

    // Frozen inception decision: replay the base run's exercise-at-t=0 choice rather than
    // re-deciding under the bumped scenario, for the same reason every other date's
    // decision is frozen (see LsmPolicy::exercise_at_inception).
    if (policy.exercise_at_inception) {
        return intrinsic(spot, strike, type);
    }
    return acc.mean();
}

} // namespace mcd::pricers
