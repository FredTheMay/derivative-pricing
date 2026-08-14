#pragma once

#include <array>
#include <cstdint>

namespace mcd {

using PhiloxCounter = std::array<std::uint32_t, 4>;
using PhiloxKey = std::array<std::uint32_t, 2>;

// Philox 4x32-10: a stateless, counter-based RNG (Salmon, Moraes, Dror, Shaw
// 2011). Given the same (counter, key), always returns the same output --
// this is what makes path i's random stream independent of thread count or
// call order, with no mutable RNG state to race on.
[[nodiscard]] PhiloxCounter philox4x32_10(PhiloxCounter counter, PhiloxKey key) noexcept;

[[nodiscard]] PhiloxKey make_philox_key(std::uint64_t seed) noexcept;

// path_index occupies counter words 0-1, draw_index occupies word 2. `stream`
// occupies word 3 -- 0 for the price-advancing normal draw, 1 for the
// Brownian-bridge crossing-check uniform (Phase 3). See
// docs/design/02-monte-carlo-core.md sec.4 and
// docs/design/03-exotics-variance-reduction.md sec.7.
[[nodiscard]] PhiloxCounter make_philox_counter(std::uint64_t path_index,
                                                 std::uint32_t draw_index = 0,
                                                 std::uint32_t stream = 0) noexcept;

// The standard normal draw for a given path/draw index under a seed: composes
// philox4x32_10 with the uniform-from-bits conversion and the inverse normal
// CDF (core/normal.hpp). Always stream 0.
[[nodiscard]] double standard_normal_variate(std::uint64_t seed, std::uint64_t path_index,
                                              std::uint32_t draw_index = 0) noexcept;

// A uniform draw on the open interval (0,1) for a given path/draw index,
// on stream 1 -- independent of the stream-0 normal at the same path/draw
// index. Used by the Brownian-bridge barrier correction.
[[nodiscard]] double uniform_variate(std::uint64_t seed, std::uint64_t path_index,
                                      std::uint32_t draw_index) noexcept;

} // namespace mcd
