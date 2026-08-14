#pragma once

#include <cmath>
#include <cstdint>

namespace mcd {

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

  private:
    std::uint64_t count_ = 0;
    double mean_ = 0.0;
    double m2_ = 0.0;
};

} // namespace mcd
