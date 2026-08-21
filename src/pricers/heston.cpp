#include "mcd/pricers/heston.hpp"

#include "mcd/core/gauss_legendre.hpp"
#include "mcd/core/normal.hpp"
#include "mcd/core/rng.hpp"
#include "mcd/core/stats.hpp"

#include <algorithm>
#include <cmath>
#include <complex>
#include <numbers>

namespace mcd::pricers {

namespace {

using cd = std::complex<double>;

// The Heston (1993) characteristic function of the log return X_T = ln(S_T/S_0)
// under the risk-neutral (money-market numeraire) measure, in the branch-cut-safe
// "Little Trap" form (Albrecher, Mayer, Schoutens & Tistaert 2007; docs/design/12-
// heston.md sec.4). Valid for any complex u -- heston_semi_analytic below evaluates
// it at both real u (for P2) and u - i (for P1, via the standard measure-change
// identity phi_1(u) = phi(u-i)/phi(-i), avoiding a second, separately-derived
// characteristic function).
cd heston_char_function(const models::HestonParams& p, cd u) noexcept {
    const cd i(0.0, 1.0);
    const cd rho_xi_iu = p.rho * p.xi * i * u;
    const cd bmr = p.kappa - rho_xi_iu; // kappa - rho*xi*i*u
    const cd d = std::sqrt(bmr * bmr + p.xi * p.xi * (i * u + u * u));
    const cd c = (bmr - d) / (bmr + d);
    const cd exp_neg_dt = std::exp(-d * p.time);

    const cd big_c = i * u * (p.rate - p.carry_yield) * p.time +
                      (p.kappa * p.theta / (p.xi * p.xi)) *
                          ((bmr - d) * p.time - 2.0 * std::log((1.0 - c * exp_neg_dt) / (1.0 - c)));
    const cd big_d = (bmr - d) / (p.xi * p.xi) * (1.0 - exp_neg_dt) / (1.0 - c * exp_neg_dt);

    return std::exp(big_c + big_d * p.v0);
}

constexpr double kIntegrationUpperBound = 200.0; // sec.4: chosen empirically, see
                                                  // tests/heston_test.cpp's truncation check.
constexpr int kQuadratureOrder = 128;

} // namespace

double heston_semi_analytic(const models::HestonParams& params, double strike,
                             OptionType type) noexcept {
    const cd i(0.0, 1.0);
    const double log_moneyness = std::log(strike / params.spot);
    const cd phi_neg_i = heston_char_function(params, cd(0.0, -1.0));

    const auto p1_integrand = [&](double u) noexcept -> double {
        const cd phi_u_minus_i = heston_char_function(params, cd(u, -1.0));
        const cd value =
            std::exp(-i * u * log_moneyness) * (phi_u_minus_i / phi_neg_i) / (i * u);
        return value.real();
    };
    const auto p2_integrand = [&](double u) noexcept -> double {
        const cd phi_u = heston_char_function(params, cd(u, 0.0));
        const cd value = std::exp(-i * u * log_moneyness) * phi_u / (i * u);
        return value.real();
    };

    const double p1 = 0.5 + gauss_legendre_integrate(p1_integrand, 0.0, kIntegrationUpperBound,
                                                       kQuadratureOrder) /
                                 std::numbers::pi;
    const double p2 = 0.5 + gauss_legendre_integrate(p2_integrand, 0.0, kIntegrationUpperBound,
                                                       kQuadratureOrder) /
                                 std::numbers::pi;

    const double discount_q = std::exp(-params.carry_yield * params.time);
    const double discount_r = std::exp(-params.rate * params.time);
    const double call = params.spot * discount_q * p1 - strike * discount_r * p2;

    if (type == OptionType::Call) {
        return call;
    }
    // Put-call parity: C - P = S*e^(-qT) - K*e^(-rT).
    return call - params.spot * discount_q + strike * discount_r;
}

namespace {

// One Andersen (2008) QE step: given v_t, draws the raw stream-1 uniform once and
// either transforms it (inverse-CDF) into Z_v for the low-noise branch or uses it
// directly as U_v for the high-noise branch -- exactly one Philox call per
// (path, step) for the variance side, no wasted draws (docs/design/12-heston.md
// sec.3).
double qe_variance_step(double v_t, double kappa, double theta, double xi, double dt,
                         double raw_uniform) noexcept {
    constexpr double kPsiCritical = 1.5;

    const double exp_neg_kappa_dt = std::exp(-kappa * dt);
    const double m = theta + (v_t - theta) * exp_neg_kappa_dt;
    const double s2 = v_t * xi * xi * exp_neg_kappa_dt / kappa * (1.0 - exp_neg_kappa_dt) +
                       theta * xi * xi / (2.0 * kappa) * (1.0 - exp_neg_kappa_dt) *
                           (1.0 - exp_neg_kappa_dt);
    const double psi = s2 / (m * m);

    if (psi <= kPsiCritical) {
        const double inv_psi = 1.0 / psi;
        const double b2 = 2.0 * inv_psi - 1.0 + std::sqrt(2.0 * inv_psi) * std::sqrt(2.0 * inv_psi - 1.0);
        const double a = m / (1.0 + b2);
        const double z_v = inverse_standard_normal_cdf(raw_uniform);
        const double b = std::sqrt(b2);
        return a * (b + z_v) * (b + z_v);
    }
    const double p = (psi - 1.0) / (psi + 1.0);
    const double beta = (1.0 - p) / m;
    if (raw_uniform <= p) {
        return 0.0;
    }
    return std::log((1.0 - p) / (1.0 - raw_uniform)) / beta;
}

} // namespace

HestonMcResult heston_qe_european(const models::HestonParams& params, double strike,
                                   OptionType type, int monitoring_points,
                                   std::uint64_t path_count, std::uint64_t seed) noexcept {
    const double dt = params.time / static_cast<double>(monitoring_points);
    const double drift = (params.rate - params.carry_yield) * dt;
    const double k0 = -params.rho * params.kappa * params.theta * dt / params.xi;
    const double k1 = 0.5 * dt * (params.kappa * params.rho / params.xi - 0.5) - params.rho / params.xi;
    const double k2 = 0.5 * dt * (params.kappa * params.rho / params.xi - 0.5) + params.rho / params.xi;
    const double k3 = 0.5 * dt * (1.0 - params.rho * params.rho);
    const double k4 = k3;
    const double discount = std::exp(-params.rate * params.time);
    const double phi = type == OptionType::Call ? 1.0 : -1.0;
    const double log_s0 = std::log(params.spot);

    WelfordAccumulator acc;
    for (std::uint64_t path_index = 0; path_index < path_count; ++path_index) {
        double v = params.v0;
        double log_s = log_s0;
        for (int step = 0; step < monitoring_points; ++step) {
            const auto draw_index = static_cast<std::uint32_t>(step);
            const double z_s = standard_normal_variate(seed, path_index, draw_index);
            const double raw_uniform = uniform_variate(seed, path_index, draw_index);

            const double v_next = qe_variance_step(v, params.kappa, params.theta, params.xi, dt,
                                                     raw_uniform);
            const double diffusion_var = std::max(k3 * v + k4 * v_next, 0.0);
            log_s += drift + k0 + k1 * v + k2 * v_next + std::sqrt(diffusion_var) * z_s;
            v = v_next;
        }
        const double s_t = std::exp(log_s);
        const double payoff = std::max(phi * (s_t - strike), 0.0);
        acc.add(discount * payoff);
    }

    return HestonMcResult{.price = acc.mean(), .standard_error = acc.standard_error(),
                           .path_count = acc.count()};
}

} // namespace mcd::pricers
