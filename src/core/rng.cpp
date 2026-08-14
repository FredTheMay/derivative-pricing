#include "mcd/core/rng.hpp"

#include "mcd/core/normal.hpp"

namespace mcd {

namespace {

constexpr std::uint32_t kMultiplier0 = 0xD2511F53u;
constexpr std::uint32_t kMultiplier1 = 0xCD9E8D57u;
constexpr std::uint32_t kWeyl0 = 0x9E3779B9u;
constexpr std::uint32_t kWeyl1 = 0xBB67AE85u;
constexpr int kRounds = 10;

struct HiLo {
    std::uint32_t hi;
    std::uint32_t lo;
};

HiLo mul_hi_lo(std::uint32_t a, std::uint32_t b) noexcept {
    const std::uint64_t product = static_cast<std::uint64_t>(a) * static_cast<std::uint64_t>(b);
    return {static_cast<std::uint32_t>(product >> 32), static_cast<std::uint32_t>(product)};
}

PhiloxCounter philox_round(PhiloxCounter ctr, PhiloxKey key) noexcept {
    const HiLo m0 = mul_hi_lo(kMultiplier0, ctr[0]);
    const HiLo m1 = mul_hi_lo(kMultiplier1, ctr[2]);
    return {m1.hi ^ ctr[1] ^ key[0], m1.lo, m0.hi ^ ctr[3] ^ key[1], m0.lo};
}

PhiloxKey bump_key(PhiloxKey key) noexcept { return {key[0] + kWeyl0, key[1] + kWeyl1}; }

double uniform_open01(std::uint32_t bits) noexcept {
    constexpr double kTwoPow32 = 4294967296.0;
    return (static_cast<double>(bits) + 0.5) / kTwoPow32;
}

} // namespace

PhiloxCounter philox4x32_10(PhiloxCounter counter, PhiloxKey key) noexcept {
    for (int round = 0; round < kRounds; ++round) {
        counter = philox_round(counter, key);
        key = bump_key(key);
    }
    return counter;
}

PhiloxKey make_philox_key(std::uint64_t seed) noexcept {
    return {static_cast<std::uint32_t>(seed), static_cast<std::uint32_t>(seed >> 32)};
}

PhiloxCounter make_philox_counter(std::uint64_t path_index, std::uint32_t draw_index,
                                   std::uint32_t stream) noexcept {
    return {static_cast<std::uint32_t>(path_index), static_cast<std::uint32_t>(path_index >> 32),
            draw_index, stream};
}

double standard_normal_variate(std::uint64_t seed, std::uint64_t path_index,
                                std::uint32_t draw_index) noexcept {
    const PhiloxKey key = make_philox_key(seed);
    const PhiloxCounter counter = make_philox_counter(path_index, draw_index, /*stream=*/0);
    const PhiloxCounter output = philox4x32_10(counter, key);
    const double u = uniform_open01(output[0]);
    return inverse_standard_normal_cdf(u);
}

double uniform_variate(std::uint64_t seed, std::uint64_t path_index,
                        std::uint32_t draw_index) noexcept {
    const PhiloxKey key = make_philox_key(seed);
    const PhiloxCounter counter = make_philox_counter(path_index, draw_index, /*stream=*/1);
    const PhiloxCounter output = philox4x32_10(counter, key);
    return uniform_open01(output[0]);
}

} // namespace mcd
