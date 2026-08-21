#include "mcd/models/brownian_bridge_path.hpp"

#include <cmath>

namespace mcd::models {

namespace {

// A pending [left, right) index pair still awaiting a bisection. left == -1 means "the
// path origin, t=0, W=0" (there is no explicit index for it in `filled`/`w`). Sized for
// the Stretch-Goal-3 dimension cap (mcd::kSobolMaxDimensions == 7); see
// docs/design/10-sobol-qmc.md sec.4.
struct Interval {
    int left;
    int right;
};

} // namespace

std::vector<double> brownian_bridge_gbm_path(const GbmParams& params, int monitoring_points,
                                              std::span<const double> normals) noexcept {
    const int n = monitoring_points;
    std::vector<double> t(static_cast<std::size_t>(n) + 1);
    for (int i = 0; i <= n; ++i) {
        t[static_cast<std::size_t>(i)] =
            static_cast<double>(i) * params.time / static_cast<double>(n);
    }

    std::vector<double> w(static_cast<std::size_t>(n), 0.0);

    // normals[0] drives the terminal point directly: W(T) = sqrt(T) * z.
    w[static_cast<std::size_t>(n - 1)] = std::sqrt(t[static_cast<std::size_t>(n)]) * normals[0];

    if (n > 1) {
        // Breadth-first bisection queue. At most n-1 real splits occur (one per
        // remaining point), each pushing 2 children, plus the 1 initial interval: an
        // upper bound of 2*(n-1) + 1 entries ever pushed.
        std::vector<Interval> queue;
        queue.reserve(static_cast<std::size_t>(2 * n - 1));
        std::size_t head = 0;
        queue.push_back(Interval{-1, n - 1});

        int draw = 1;
        while (head < queue.size() && draw < n) {
            const Interval iv = queue[head++];
            if (iv.right - iv.left <= 1) {
                continue;
            }
            const int mid = (iv.left + iv.right) / 2;

            const double t_left = iv.left == -1 ? 0.0 : t[static_cast<std::size_t>(iv.left + 1)];
            const double w_left = iv.left == -1 ? 0.0 : w[static_cast<std::size_t>(iv.left)];
            const double t_right = t[static_cast<std::size_t>(iv.right + 1)];
            const double w_right = w[static_cast<std::size_t>(iv.right)];
            const double t_mid = t[static_cast<std::size_t>(mid + 1)];

            const double mean =
                w_left + (t_mid - t_left) / (t_right - t_left) * (w_right - w_left);
            const double variance = (t_mid - t_left) * (t_right - t_mid) / (t_right - t_left);

            w[static_cast<std::size_t>(mid)] = mean + std::sqrt(variance) * normals[static_cast<std::size_t>(draw)];
            ++draw;

            queue.push_back(Interval{iv.left, mid});
            queue.push_back(Interval{mid, iv.right});
        }
    }

    std::vector<double> spots(static_cast<std::size_t>(n));
    const double drift_rate = params.rate - params.carry_yield - 0.5 * params.vol * params.vol;
    for (int i = 0; i < n; ++i) {
        const double drift = drift_rate * t[static_cast<std::size_t>(i + 1)];
        spots[static_cast<std::size_t>(i)] =
            params.spot * std::exp(drift + params.vol * w[static_cast<std::size_t>(i)]);
    }
    return spots;
}

} // namespace mcd::models
