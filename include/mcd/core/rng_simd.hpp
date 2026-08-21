#pragma once

#include <array>
#include <cstdint>

namespace mcd {

// Stretch Goal 4 (docs/design/11-simd.md): ARM NEON is the target ISA -- it matches
// both this dev machine and the deployed AWS Lambda (ARM64/Graviton), and is fully
// verifiable and benchmarkable locally, unlike an AVX2 path that would only ever run
// inside a container. On non-ARM builds (the x86_64 CI matrix) kHasNeon is false and
// standard_normal_variate_batch4 falls back to four scalar standard_normal_variate
// calls -- callers never need to branch on availability themselves.
#if defined(__ARM_NEON) || defined(__aarch64__)
inline constexpr bool kHasNeon = true;
#else
inline constexpr bool kHasNeon = false;
#endif

// Computes standard_normal_variate(seed, path_index_base + i, draw_index) for i in
// 0..3. MUST be bitwise-identical to four individual scalar calls -- see
// docs/design/11-simd.md sec.4/5 for why that holds (Philox is pure integer
// arithmetic; the inverse CDF has FP contraction explicitly disabled in
// rng_simd.cpp so the scalar and NEON paths round identically).
[[nodiscard]] std::array<double, 4> standard_normal_variate_batch4(
    std::uint64_t seed, std::uint64_t path_index_base, std::uint32_t draw_index = 0) noexcept;

} // namespace mcd
