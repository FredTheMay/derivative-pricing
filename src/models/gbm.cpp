#include "mcd/models/gbm.hpp"

#include <cmath>

namespace mcd::models {

double gbm_terminal_spot(const GbmParams& params, double standard_normal) noexcept {
    const double drift = (params.rate - params.carry_yield - 0.5 * params.vol * params.vol) *
                          params.time;
    const double diffusion = params.vol * std::sqrt(params.time) * standard_normal;
    return params.spot * std::exp(drift + diffusion);
}

} // namespace mcd::models
