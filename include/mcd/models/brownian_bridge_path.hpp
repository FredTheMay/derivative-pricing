#pragma once

#include "mcd/models/gbm.hpp"

#include <span>
#include <vector>

namespace mcd::models {

// Brownian-bridge path construction (Caflisch-Morokoff-Owen 1997) -- distinct from
// Phase 3's Brownian-bridge *continuity correction* for discretely-monitored barriers
// (docs/design/03-exotics-variance-reduction.md). Maps `normals[0..monitoring_points-1]`
// (already inverse-CDF-transformed to standard normals, one per Sobol dimension) onto
// the path's monitoring dates by recursive bisection: normals[0] drives the terminal
// point, normals[1] the midpoint, normals[2] the quarter-points, and so on -- so the
// low (best-discrepancy) Sobol dimensions carry the most important part of the path.
// See docs/design/10-sobol-qmc.md sec.3.
//
// Returns the GBM spot at each of the `monitoring_points` monitoring dates
// t_i = i * params.time / monitoring_points, i = 1..monitoring_points, in chronological
// order (not construction order). Caller must ensure monitoring_points >= 1 and
// normals.size() >= monitoring_points; not validated here (same trust-the-caller
// convention as the rest of mcd::pricers -- see docs/design/10-sobol-qmc.md sec.5).
[[nodiscard]] std::vector<double> brownian_bridge_gbm_path(const GbmParams& params,
                                                             int monitoring_points,
                                                             std::span<const double> normals) noexcept;

} // namespace mcd::models
