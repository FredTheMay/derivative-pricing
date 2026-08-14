#pragma once

#include "mcd/core/types.hpp"

#include <cstdint>

namespace mcd::pricers {

struct McResult {
    double price;
    double standard_error;
    std::uint64_t path_count;
};

// Variance-reduction switches. Each pricer documents which fields it
// respects; unused fields are ignored rather than erroring, since a single
// shared options struct is simpler than a bespoke one per pricer.
struct McOptions {
    // Mirrors every step's draws (-z) through a second path and accumulates
    // the pair average as one sample. Respected by every pricer below.
    bool antithetic = false;
    // Geometric-Asian control variate. Respected only by monte_carlo_asian
    // when style == AverageStyle::Arithmetic.
    bool control_variate = false;
    // Brownian-bridge continuity correction for discrete monitoring.
    // Respected only by monte_carlo_barrier.
    bool brownian_bridge = false;
    // Worker threads for the parallel path loop. 1 = single-threaded,
    // identical code path and bitwise-identical result to Phase 2/3 (see
    // docs/design/04-parallelism.md). Respected by every pricer below except
    // the zero-options monte_carlo_european overload, which is always 1.
    unsigned num_threads = 1;
};

// Single-threaded Monte Carlo European pricer. Streaming (no path storage),
// zero heap allocation in the pricing loop. Deterministic: identical
// (seed, path_count) always produce a bitwise-identical result.
[[nodiscard]] McResult monte_carlo_european(double spot, double strike, double rate,
                                             double carry_yield, double vol, double time,
                                             OptionType type, std::uint64_t path_count,
                                             std::uint64_t seed) noexcept;

// Overload adding variance-reduction options, without changing the Phase 2
// signature or behavior above (which is exactly this overload with
// options == McOptions{}).
[[nodiscard]] McResult monte_carlo_european(double spot, double strike, double rate,
                                             double carry_yield, double vol, double time,
                                             OptionType type, std::uint64_t path_count,
                                             std::uint64_t seed, McOptions options) noexcept;

[[nodiscard]] McResult monte_carlo_digital(double spot, double strike, double rate,
                                            double carry_yield, double vol, double time,
                                            OptionType type, DigitalStyle style,
                                            double cash_amount, std::uint64_t path_count,
                                            std::uint64_t seed,
                                            McOptions options = {}) noexcept;

// monitoring_points is the number of discrete observation dates across
// (0, T]; the averaging/extremum-tracking/breach-checking convention is
// documented in docs/design/03-exotics-variance-reduction.md sec.2.

[[nodiscard]] McResult monte_carlo_asian(double spot, double strike, double rate,
                                          double carry_yield, double vol, double time,
                                          OptionType type, StrikeStyle strike_style,
                                          AverageStyle average_style, int monitoring_points,
                                          std::uint64_t path_count, std::uint64_t seed,
                                          McOptions options = {}) noexcept;

[[nodiscard]] McResult monte_carlo_barrier(double spot, double strike, double barrier, double rate,
                                            double carry_yield, double vol, double time,
                                            OptionType type, BarrierDirection direction,
                                            BarrierKnock knock, double rebate,
                                            int monitoring_points, std::uint64_t path_count,
                                            std::uint64_t seed, McOptions options = {}) noexcept;

[[nodiscard]] McResult monte_carlo_lookback(double spot, double strike, double rate,
                                             double carry_yield, double vol, double time,
                                             OptionType type, StrikeStyle style,
                                             int monitoring_points, std::uint64_t path_count,
                                             std::uint64_t seed, McOptions options = {}) noexcept;

} // namespace mcd::pricers
