// FP contraction is disabled for this file AND src/core/normal.cpp via
// -ffp-contract=off (CMakeLists.txt, not a #pragma -- GCC does not recognize
// #pragma STDC FP_CONTRACT). The scalar standard_normal_variate (rng.cpp/normal.cpp)
// and the NEON path below must be bitwise identical, and a fused-multiply-add rounds
// once instead of twice -- letting the compiler auto-contract one path and not the
// other would silently break that. See docs/design/11-simd.md sec.4. Narrow and
// explicit, unlike -ffast-math (which CLAUDE.md forbids for unrelated reasons): this
// only controls FMA fusion, nothing about NaN/Inf/signed-zero/reciprocal-approximation
// semantics.

#include "mcd/core/rng_simd.hpp"

#include "mcd/core/normal.hpp"
#include "mcd/core/rng.hpp"

#if defined(__ARM_NEON) || defined(__aarch64__)
#include <arm_neon.h>
#include <cmath>
#include <initializer_list>
#include <numbers>
#endif

namespace mcd {

#if defined(__ARM_NEON) || defined(__aarch64__)

namespace {

constexpr std::uint32_t kMultiplier0 = 0xD2511F53u;
constexpr std::uint32_t kMultiplier1 = 0xCD9E8D57u;
constexpr std::uint32_t kWeyl0 = 0x9E3779B9u;
constexpr std::uint32_t kWeyl1 = 0xBB67AE85u;
constexpr int kRounds = 10;

struct Ctr4 {
    uint32x4_t w0, w1, w2, w3;
};

// Widening 32x32->64 multiply across 4 lanes, split hi/lo -- exact by construction,
// no rounding exists for integer multiplication, so this is unconditionally bitwise
// identical to the scalar mul_hi_lo (rng.cpp) applied 4 times.
void mul_hi_lo4(uint32x4_t a, uint32x4_t b, uint32x4_t& hi, uint32x4_t& lo) noexcept {
    const uint64x2_t lo_pair = vmull_u32(vget_low_u32(a), vget_low_u32(b));
    const uint64x2_t hi_pair = vmull_high_u32(a, b);
    const uint32x4_t lo_pair_u32 = vreinterpretq_u32_u64(lo_pair);
    const uint32x4_t hi_pair_u32 = vreinterpretq_u32_u64(hi_pair);
    lo = vuzp1q_u32(lo_pair_u32, hi_pair_u32); // [prod0.lo, prod1.lo, prod2.lo, prod3.lo]
    hi = vuzp2q_u32(lo_pair_u32, hi_pair_u32); // [prod0.hi, prod1.hi, prod2.hi, prod3.hi]
}

Ctr4 philox_round4(Ctr4 ctr, uint32x4_t key0, uint32x4_t key1) noexcept {
    uint32x4_t m0_hi, m0_lo, m1_hi, m1_lo;
    mul_hi_lo4(vdupq_n_u32(kMultiplier0), ctr.w0, m0_hi, m0_lo);
    mul_hi_lo4(vdupq_n_u32(kMultiplier1), ctr.w2, m1_hi, m1_lo);
    return {.w0 = veorq_u32(veorq_u32(m1_hi, ctr.w1), key0),
            .w1 = m1_lo,
            .w2 = veorq_u32(veorq_u32(m0_hi, ctr.w3), key1),
            .w3 = m0_lo};
}

// Vectorised Philox4x32-10 across 4 independent (path_index, key) lanes -- word 0/1
// vary per lane (the counter's own path-index words), word 2/3 (draw_index, stream)
// are identical across lanes within one batch4 call, per make_philox_counter's
// layout (rng.hpp).
Ctr4 philox4x32_10_x4(Ctr4 ctr, uint32x4_t key0, uint32x4_t key1) noexcept {
    for (int round = 0; round < kRounds; ++round) {
        ctr = philox_round4(ctr, key0, key1);
        key0 = vaddq_u32(key0, vdupq_n_u32(kWeyl0));
        key1 = vaddq_u32(key1, vdupq_n_u32(kWeyl1));
    }
    return ctr;
}

float64x2_t uniform_open01_x2(uint32x2_t bits) noexcept {
    constexpr double kTwoPow32 = 4294967296.0;
    const float64x2_t as_double = vcvtq_f64_u64(vmovl_u32(bits));
    return vdivq_f64(vaddq_f64(as_double, vdupq_n_f64(0.5)), vdupq_n_f64(kTwoPow32));
}

// Horner evaluation, 2-wide, using separate multiply+add (never vfmaq_f64) so this
// matches the scalar Horner chain in normal.cpp bit-for-bit under FP_CONTRACT OFF --
// a fused multiply-add rounds once instead of twice and would silently diverge.
float64x2_t horner2(std::initializer_list<double> coeffs, float64x2_t x) noexcept {
    auto it = coeffs.begin();
    float64x2_t acc = vdupq_n_f64(*it++);
    for (; it != coeffs.end(); ++it) {
        acc = vaddq_f64(vmulq_f64(acc, x), vdupq_n_f64(*it));
    }
    return acc;
}

// 2-wide Acklam inverse CDF, structurally identical to normal.cpp's scalar version:
// same branch condition (per-lane select instead of an if), same Horner coefficient
// order, same single Halley refinement step. Required for bitwise identity with the
// scalar path (docs/design/11-simd.md sec.4/5), not just numerical closeness.
float64x2_t inverse_standard_normal_cdf_x2(float64x2_t u) noexcept {
    constexpr double kLowBreak = 0.02425;
    constexpr double kHighBreak = 1.0 - kLowBreak;

    const uint64x2_t is_low = vcltq_f64(u, vdupq_n_f64(kLowBreak));
    const uint64x2_t is_high = vcgtq_f64(u, vdupq_n_f64(kHighBreak));
    const uint64x2_t is_tail = vorrq_u64(is_low, is_high);

    const double u0 = vgetq_lane_f64(u, 0);
    const double u1 = vgetq_lane_f64(u, 1);
    const bool low0 = vgetq_lane_u64(is_low, 0) != 0;
    const bool low1 = vgetq_lane_u64(is_low, 1) != 0;

    // Tail branch: z = sqrt(-2*log(lower ? u : 1-u)). No portable NEON log
    // intrinsic exists (and none would be guaranteed bit-identical to std::log
    // anyway), so this step is per-lane -- exactly what the scalar path does too.
    const double tail_u0 = low0 ? u0 : 1.0 - u0;
    const double tail_u1 = low1 ? u1 : 1.0 - u1;
    const float64x2_t z_tail =
        vsetq_lane_f64(std::sqrt(-2.0 * std::log(tail_u1)),
                        vsetq_lane_f64(std::sqrt(-2.0 * std::log(tail_u0)), vdupq_n_f64(0.0), 0), 1);

    const float64x2_t c_num = horner2({-7.784894002430293e-03, -3.223964580411365e-01,
                                        -2.400758277161838e+00, -2.549732539343734e+00,
                                        4.374664141464968e+00, 2.938163982698783e+00},
                                       z_tail);
    const float64x2_t d_den = horner2({7.784695709041462e-03, 3.224671290700398e-01,
                                        2.445134137142996e+00, 3.754408661907416e+00, 1.0},
                                       z_tail);
    const float64x2_t tail_val = vdivq_f64(c_num, d_den);
    const float64x2_t tail_result = vbslq_f64(is_low, tail_val, vnegq_f64(tail_val));

    // Central branch.
    const float64x2_t z_c = vsubq_f64(u, vdupq_n_f64(0.5));
    const float64x2_t r = vmulq_f64(z_c, z_c);
    const float64x2_t a_num = vmulq_f64(
        horner2({-3.969683028665376e+01, 2.209460984245205e+02, -2.759285104469687e+02,
                 1.383577518672690e+02, -3.066479806614716e+01, 2.506628277459239e+00},
                r),
        z_c);
    const float64x2_t b_den =
        horner2({-5.447609879822406e+01, 1.615858368580409e+02, -1.556989798598866e+02,
                  6.680131188771972e+01, -1.328068155288572e+01, 1.0},
                 r);
    const float64x2_t central_val = vdivq_f64(a_num, b_den);

    const float64x2_t z0_vec = vbslq_f64(is_tail, tail_result, central_val);

    // Single Halley refinement step -- needs erfc, so per-lane, matching normal.cpp.
    double z0 = vgetq_lane_f64(z0_vec, 0);
    double z1 = vgetq_lane_f64(z0_vec, 1);
    const double sqrt_2pi = std::sqrt(2.0 * std::numbers::pi);
    auto halley = [sqrt_2pi](double z, double uu) noexcept {
        const double cdf = 0.5 * std::erfc(-z / std::numbers::sqrt2);
        const double error = cdf - uu;
        const double r_ = error * sqrt_2pi * std::exp(0.5 * z * z);
        return z - r_ / (1.0 + 0.5 * z * r_);
    };
    z0 = halley(z0, u0);
    z1 = halley(z1, u1);

    return vsetq_lane_f64(z1, vsetq_lane_f64(z0, vdupq_n_f64(0.0), 0), 1);
}

} // namespace

std::array<double, 4> standard_normal_variate_batch4(std::uint64_t seed,
                                                       std::uint64_t path_index_base,
                                                       std::uint32_t draw_index) noexcept {
    alignas(16) std::uint32_t word0[4];
    alignas(16) std::uint32_t word1[4];
    for (int i = 0; i < 4; ++i) {
        const std::uint64_t path_index = path_index_base + static_cast<std::uint64_t>(i);
        word0[i] = static_cast<std::uint32_t>(path_index);
        word1[i] = static_cast<std::uint32_t>(path_index >> 32);
    }

    const PhiloxKey key = make_philox_key(seed);
    const Ctr4 ctr{.w0 = vld1q_u32(word0),
                    .w1 = vld1q_u32(word1),
                    .w2 = vdupq_n_u32(draw_index),
                    .w3 = vdupq_n_u32(0)};
    const Ctr4 out = philox4x32_10_x4(ctr, vdupq_n_u32(key[0]), vdupq_n_u32(key[1]));

    alignas(16) std::uint32_t out_word0[4];
    vst1q_u32(out_word0, out.w0);

    const float64x2_t u_lo = uniform_open01_x2(vld1_u32(out_word0));
    const float64x2_t u_hi = uniform_open01_x2(vld1_u32(out_word0 + 2));
    const float64x2_t z_lo = inverse_standard_normal_cdf_x2(u_lo);
    const float64x2_t z_hi = inverse_standard_normal_cdf_x2(u_hi);

    return {vgetq_lane_f64(z_lo, 0), vgetq_lane_f64(z_lo, 1), vgetq_lane_f64(z_hi, 0),
            vgetq_lane_f64(z_hi, 1)};
}

#else

std::array<double, 4> standard_normal_variate_batch4(std::uint64_t seed,
                                                       std::uint64_t path_index_base,
                                                       std::uint32_t draw_index) noexcept {
    return {standard_normal_variate(seed, path_index_base + 0, draw_index),
            standard_normal_variate(seed, path_index_base + 1, draw_index),
            standard_normal_variate(seed, path_index_base + 2, draw_index),
            standard_normal_variate(seed, path_index_base + 3, draw_index)};
}

#endif

} // namespace mcd
