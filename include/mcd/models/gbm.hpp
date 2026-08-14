#pragma once

namespace mcd::models {

struct GbmParams {
    double spot;
    double rate;
    double carry_yield;
    double vol;
    double time;
};

// Exact solution of geometric Brownian motion at time `params.time`, given one
// standard normal draw -- not an Euler discretization of the SDE:
// S_T = S0 * exp((r - q - 0.5*sigma^2)*T + sigma*sqrt(T)*Z).
[[nodiscard]] double gbm_terminal_spot(const GbmParams& params, double standard_normal) noexcept;

} // namespace mcd::models
