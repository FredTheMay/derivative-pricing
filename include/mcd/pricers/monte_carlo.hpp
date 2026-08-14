#pragma once

#include "mcd/core/types.hpp"

#include <cstdint>

namespace mcd::pricers {

struct McResult {
    double price;
    double standard_error;
    std::uint64_t path_count;
};

// Single-threaded Monte Carlo European pricer. Streaming (no path storage),
// zero heap allocation in the pricing loop. Deterministic: identical
// (seed, path_count) always produce a bitwise-identical result.
[[nodiscard]] McResult monte_carlo_european(double spot, double strike, double rate,
                                             double carry_yield, double vol, double time,
                                             OptionType type, std::uint64_t path_count,
                                             std::uint64_t seed) noexcept;

} // namespace mcd::pricers
