#pragma once

#include "mcd/core/types.hpp"

#include <cstdint>

namespace mcd::pricers {

struct QmcResult {
    double price = 0.0;
    // No standard_error: Sobol is a deterministic low-discrepancy sequence, not a
    // random variable in the sense plain Monte Carlo's paths are -- there is no
    // meaningful per-path variance to report. See docs/design/10-sobol-qmc.md sec.6
    // for how this project measures QMC accuracy instead (absolute error against the
    // known analytic price, not a statistical standard error).
};

// Sobol-QMC European pricer, 1 dimension (Sobol dimension 0). No `seed`: Sobol
// sequences are deterministic given (dimension, index); there is nothing to
// randomize. path_count is the number of Sobol points used (index 1..path_count;
// index 0 is skipped, since sobol_point(_, 0) == 0.0 maps to an invalid normal draw).
[[nodiscard]] QmcResult qmc_sobol_european(double spot, double strike, double rate,
                                            double carry_yield, double vol, double time,
                                            OptionType type, std::uint64_t path_count) noexcept;

// Sobol-QMC arithmetic-average Asian pricer (fixed strike), using Brownian-bridge path
// construction (mcd::models::brownian_bridge_gbm_path) across `monitoring_points`
// Sobol dimensions. Caller must ensure 1 <= monitoring_points <= mcd::kSobolMaxDimensions
// (7); not validated here -- enforced as request validation at the mcd_cli/bindings/AWS
// layer instead, per docs/design/10-sobol-qmc.md sec.4-5.
[[nodiscard]] QmcResult qmc_sobol_asian(double spot, double strike, double rate,
                                         double carry_yield, double vol, double time,
                                         OptionType type, StrikeStyle strike_style,
                                         int monitoring_points, std::uint64_t path_count) noexcept;

} // namespace mcd::pricers
