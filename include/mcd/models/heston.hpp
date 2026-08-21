#pragma once

namespace mcd::models {

// Heston (1993) stochastic volatility, Stretch Goal 5 (docs/design/12-heston.md):
// dS = (r-q)*S*dt + sqrt(v)*S*dW^S
// dv = kappa*(theta-v)*dt + xi*sqrt(v)*dW^v,  corr(dW^S, dW^v) = rho*dt
struct HestonParams {
    double spot;
    double rate;
    double carry_yield;
    double v0;    // initial variance
    double kappa; // mean-reversion speed
    double theta; // long-run variance
    double xi;    // vol-of-vol
    double rho;   // spot/variance correlation
    double time;
};

} // namespace mcd::models
