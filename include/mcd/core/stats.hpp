#pragma once

#include <cmath>
#include <cstdint>

namespace mcd {

namespace detail {
// Deliberately not std::hardware_destructive_interference_size: GCC warns
// (-Werror=interference-size) that its value is tuning-dependent and unsafe
// to bake into any layout an ABI might depend on -- confirmed concretely
// during this project's own build, where GCC reported 256 bytes for this
// exact machine's tuning against libc++'s generic 64. A fixed, explicit
// constant sidesteps that instability; 128 is a safe upper bound on common
// cache line sizes (64 on most x86/older ARM, up to 128 on some
// Apple-Silicon-class cores) without the cost of GCC's more conservative 256.
inline constexpr std::size_t kDestructiveInterferenceSize = 128;
} // namespace detail

// Welford's online algorithm: one-pass, numerically stable mean and variance.
class WelfordAccumulator {
  public:
    void add(double value) noexcept {
        ++count_;
        const double delta = value - mean_;
        mean_ += delta / static_cast<double>(count_);
        const double delta2 = value - mean_;
        m2_ += delta * delta2;
    }

    [[nodiscard]] std::uint64_t count() const noexcept { return count_; }
    [[nodiscard]] double mean() const noexcept { return mean_; }

    // Sample variance (n-1 denominator). Undefined (returns 0) for count < 2.
    [[nodiscard]] double variance() const noexcept {
        return count_ < 2 ? 0.0 : m2_ / static_cast<double>(count_ - 1);
    }

    [[nodiscard]] double standard_error() const noexcept {
        return count_ == 0 ? 0.0 : std::sqrt(variance() / static_cast<double>(count_));
    }

    // Deterministic pairwise merge (Chan, Golub, LeVeque 1979): folds `other` into *this*.
    // Merging with an empty accumulator (either side) is an exact identity, not an
    // approximation -- used by the parallel pricers' fixed chunk-index merge order to
    // handle chunks left empty when path_count < logical chunk count.
    void merge(const WelfordAccumulator& other) noexcept {
        if (other.count_ == 0) {
            return;
        }
        if (count_ == 0) {
            *this = other;
            return;
        }
        const auto combined_count = count_ + other.count_;
        const double delta = other.mean_ - mean_;
        const double other_weight = static_cast<double>(other.count_) / static_cast<double>(combined_count);
        const double new_mean = mean_ + delta * other_weight;
        const double new_m2 = m2_ + other.m2_ + delta * delta * static_cast<double>(count_) *
                                                      static_cast<double>(other.count_) /
                                                      static_cast<double>(combined_count);
        count_ = combined_count;
        mean_ = new_mean;
        m2_ = new_m2;
    }

  private:
    std::uint64_t count_ = 0;
    double mean_ = 0.0;
    double m2_ = 0.0;
};

// Padded to avoid false sharing between accumulators in a per-chunk array:
// alignas both starts each instance on its own cache line AND pads sizeof()
// to a multiple of the alignment, so consecutive array elements never share
// a line.
struct alignas(detail::kDestructiveInterferenceSize) PaddedWelford {
    WelfordAccumulator acc;
};

} // namespace mcd
