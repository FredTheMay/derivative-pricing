#pragma once

#include <cstdint>

namespace mcd {

// Hand-written Sobol low-discrepancy sequence. Deterministic given (dimension, index) --
// no seed, since QMC has nothing to randomize (unlike Philox). Capped at 7 dimensions,
// each built from a primitive polynomial over GF(2) independently verified by direct
// LFSR maximal-period simulation this session (not copied from an external
// direction-number table) -- see docs/design/10-sobol-qmc.md sec.2 for why the cap
// exists and what it trades away (discrepancy-optimality) for what it keeps (a
// from-scratch-verifiable construction, per CLAUDE.md sec.2.5).
constexpr unsigned kSobolMaxDimensions = 7;

// index == 0 returns exactly 0.0 (the sequence's defined starting point); index >= 1
// follows the standard Gray-code Sobol construction. dimension must be < kSobolMaxDimensions.
[[nodiscard]] double sobol_point(unsigned dimension, std::uint64_t index) noexcept;

} // namespace mcd
