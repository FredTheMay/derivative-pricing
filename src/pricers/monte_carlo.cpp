#include "mcd/pricers/monte_carlo.hpp"

#include "mcd/core/rng.hpp"
#include "mcd/core/stats.hpp"
#include "mcd/core/thread_pool.hpp"
#include "mcd/models/gbm.hpp"
#include "mcd/payoffs/asian.hpp"
#include "mcd/payoffs/barrier.hpp"
#include "mcd/payoffs/digital.hpp"
#include "mcd/payoffs/european.hpp"
#include "mcd/payoffs/lookback.hpp"
#include "mcd/pricers/analytic.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace mcd::pricers {

namespace {

McResult finish(const WelfordAccumulator& acc) noexcept {
    return McResult{.price = acc.mean(), .standard_error = acc.standard_error(),
                     .path_count = acc.count()};
}

// The single shared accumulation engine every pricer below funnels through. Always
// partitions [0, path_count) into ThreadPool::logical_chunk_count() static contiguous
// chunks and always merges them in fixed chunk-index order, regardless of
// options.num_threads -- this fixed computational graph, not any particular execution
// strategy, is what makes results bitwise identical across thread counts (see
// docs/design/04-parallelism.md sec.2). For num_threads <= 1, chunks are computed by a
// plain sequential loop on the calling thread -- same chunk boundaries and merge order
// as the parallel path, just no thread-pool synchronization overhead, since determinism
// comes from the algorithm, not from whether a pool literally executes it.
//
// run_one_path(path_index, negate) -> double must be pure given (path_index, negate).
template <typename RunOnePath>
McResult accumulate_paths(std::uint64_t path_count, const McOptions& options,
                           RunOnePath&& run_one_path) noexcept {
    const auto sample_at = [&](std::uint64_t path_index) -> double {
        double sample = run_one_path(path_index, false);
        if (options.antithetic) {
            const double sample2 = run_one_path(path_index, true);
            sample = 0.5 * (sample + sample2);
        }
        return sample;
    };

    const unsigned chunk_count = ThreadPool::logical_chunk_count();
    std::array<PaddedWelford, kMaxLogicalChunks> per_chunk{};

    auto compute_chunk = [&](unsigned chunk_index, std::uint64_t begin,
                              std::uint64_t end) noexcept {
        WelfordAccumulator local;
        for (std::uint64_t path_index = begin; path_index < end; ++path_index) {
            local.add(sample_at(path_index));
        }
        per_chunk[chunk_index].acc = local;
    };

    if (options.num_threads <= 1) {
        for (unsigned chunk_index = 0; chunk_index < chunk_count; ++chunk_index) {
            const auto [begin, end] = detail::chunk_bounds(chunk_index, path_count, chunk_count);
            compute_chunk(chunk_index, begin, end);
        }
    } else {
        shared_thread_pool().parallel_for(path_count, options.num_threads, compute_chunk);
    }

    WelfordAccumulator merged;
    for (unsigned i = 0; i < chunk_count; ++i) {
        merged.merge(per_chunk[i].acc);
    }
    return finish(merged);
}

// Single draw, terminal-spot-only pricing (European, Digital). Shared so the
// draw-step-payoff-discount logic isn't duplicated; does not change
// monte_carlo_european's public signature or behavior.
template <payoffs::Payoff P>
McResult monte_carlo_terminal(const models::GbmParams& params, const P& payoff,
                               std::uint64_t path_count, std::uint64_t seed,
                               const McOptions& options) noexcept {
    const double discount = std::exp(-params.rate * params.time);
    return accumulate_paths(path_count, options, [&](std::uint64_t path_index, bool negate) {
        const double z = standard_normal_variate(seed, path_index);
        return discount * payoff(models::gbm_terminal_spot(params, negate ? -z : z));
    });
}

// Multi-step, path-dependent pricing (Asian, barrier without the Brownian
// bridge, lookback). PP is constructed fresh on the stack for every path
// from payoff_args -- never heap-allocated, never storing the path itself.
template <payoffs::PathPayoff PP, typename... Args>
McResult monte_carlo_path_dependent(double spot, double rate, double carry_yield, double vol,
                                     double time, int monitoring_points,
                                     std::uint64_t path_count, std::uint64_t seed,
                                     const McOptions& options, Args... payoff_args) noexcept {
    const double dt = time / static_cast<double>(monitoring_points);
    const double discount = std::exp(-rate * time);

    return accumulate_paths(
        path_count, options, [&](std::uint64_t path_index, bool negate) -> double {
            PP payoff(payoff_args...);
            double s = spot;
            for (int step = 0; step < monitoring_points; ++step) {
                const double z = standard_normal_variate(seed, path_index,
                                                           static_cast<std::uint32_t>(step));
                const models::GbmParams step_params{.spot = s, .rate = rate,
                                                      .carry_yield = carry_yield, .vol = vol,
                                                      .time = dt};
                s = models::gbm_terminal_spot(step_params, negate ? -z : z);
                payoff.observe(s);
            }
            return discount * payoff.result();
        });
}

// Bespoke loop for the barrier's Brownian-bridge continuity correction: needs
// an extra uniform per step and the previous price, neither of which fit the
// generic PathPayoff::observe(price) interface, so this isn't routed through
// monte_carlo_path_dependent. See docs/design/03-exotics-variance-
// reduction.md sec.2 and sec.4.
McResult monte_carlo_barrier_brownian_bridge(double spot, double strike, double barrier,
                                              double rate, double carry_yield, double vol,
                                              double time, OptionType type,
                                              BarrierDirection direction, BarrierKnock knock,
                                              double rebate, int monitoring_points,
                                              std::uint64_t path_count, std::uint64_t seed,
                                              const McOptions& options) noexcept {
    const double dt = time / static_cast<double>(monitoring_points);
    const double discount = std::exp(-rate * time);
    const double log_barrier = std::log(barrier);

    return accumulate_paths(
        path_count, options, [&](std::uint64_t path_index, bool negate) -> double {
            double s = spot;
            bool breached = false;
            for (int step = 0; step < monitoring_points; ++step) {
                const auto step_u32 = static_cast<std::uint32_t>(step);
                const double z = standard_normal_variate(seed, path_index, step_u32);
                const models::GbmParams step_params{.spot = s, .rate = rate,
                                                      .carry_yield = carry_yield, .vol = vol,
                                                      .time = dt};
                const double s_next = models::gbm_terminal_spot(step_params, negate ? -z : z);

                const bool crossed_discrete =
                    direction == BarrierDirection::Up ? s_next >= barrier : s_next <= barrier;
                breached = breached || crossed_discrete;

                if (!breached) {
                    // Brownian-bridge hitting probability in log-price space (same form for
                    // both directions -- see design doc sec.4): the endpoints being on the
                    // "alive" side, guaranteed by !breached here, keeps the exponent <= 0.
                    const double log_s = std::log(s);
                    const double log_s_next = std::log(s_next);
                    const double p_cross = std::exp(-2.0 * (log_barrier - log_s) *
                                                     (log_barrier - log_s_next) /
                                                     (vol * vol * dt));
                    const double u = uniform_variate(seed, path_index, step_u32);
                    breached = u < p_cross;
                }
                s = s_next;
            }
            const double phi = type == OptionType::Call ? 1.0 : -1.0;
            const double vanilla = std::max(phi * (s - strike), 0.0);
            const bool pays_vanilla = knock == BarrierKnock::Out ? !breached : breached;
            return discount * (pays_vanilla ? vanilla : rebate);
        });
}

} // namespace

McResult monte_carlo_european(double spot, double strike, double rate, double carry_yield,
                               double vol, double time, OptionType type,
                               std::uint64_t path_count, std::uint64_t seed) noexcept {
    return monte_carlo_european(spot, strike, rate, carry_yield, vol, time, type, path_count,
                                 seed, McOptions{});
}

McResult monte_carlo_european(double spot, double strike, double rate, double carry_yield,
                               double vol, double time, OptionType type,
                               std::uint64_t path_count, std::uint64_t seed,
                               McOptions options) noexcept {
    const models::GbmParams params{.spot = spot, .rate = rate, .carry_yield = carry_yield,
                                    .vol = vol, .time = time};
    const payoffs::EuropeanPayoff payoff{.strike = strike, .type = type};
    return monte_carlo_terminal(params, payoff, path_count, seed, options);
}

McResult monte_carlo_digital(double spot, double strike, double rate, double carry_yield,
                              double vol, double time, OptionType type, DigitalStyle style,
                              double cash_amount, std::uint64_t path_count, std::uint64_t seed,
                              McOptions options) noexcept {
    const models::GbmParams params{.spot = spot, .rate = rate, .carry_yield = carry_yield,
                                    .vol = vol, .time = time};
    const payoffs::DigitalPayoff payoff{.strike = strike, .type = type, .style = style,
                                         .cash_amount = cash_amount};
    return monte_carlo_terminal(params, payoff, path_count, seed, options);
}

McResult monte_carlo_asian(double spot, double strike, double rate, double carry_yield,
                            double vol, double time, OptionType type, StrikeStyle strike_style,
                            AverageStyle average_style, int monitoring_points,
                            std::uint64_t path_count, std::uint64_t seed,
                            McOptions options) noexcept {
    if (average_style == AverageStyle::Geometric) {
        return monte_carlo_path_dependent<payoffs::GeometricAsianPayoff>(
            spot, rate, carry_yield, vol, time, monitoring_points, path_count, seed, options,
            strike, type, strike_style);
    }

    // Control variate is only meaningful with an analytic anchor for the control's own
    // price; Phase 1's kemna_vorst is fixed-strike only, so control_variate is honored
    // only for fixed-strike arithmetic Asian and otherwise falls back to plain MC.
    if (options.control_variate && strike_style == StrikeStyle::Fixed) {
        const double dt = time / static_cast<double>(monitoring_points);
        const double discount = std::exp(-rate * time);
        const double geometric_analytic =
            kemna_vorst(spot, strike, rate, carry_yield, vol, time, type);

        return accumulate_paths(
            path_count, options, [&](std::uint64_t path_index, bool negate) -> double {
                payoffs::ArithmeticAsianPayoff arithmetic{.strike = strike, .type = type,
                                                           .style = strike_style};
                payoffs::GeometricAsianPayoff geometric{.strike = strike, .type = type,
                                                         .style = strike_style};
                double s = spot;
                for (int step = 0; step < monitoring_points; ++step) {
                    const double z = standard_normal_variate(seed, path_index,
                                                               static_cast<std::uint32_t>(step));
                    const models::GbmParams step_params{.spot = s, .rate = rate,
                                                          .carry_yield = carry_yield, .vol = vol,
                                                          .time = dt};
                    s = models::gbm_terminal_spot(step_params, negate ? -z : z);
                    arithmetic.observe(s);
                    geometric.observe(s);
                }
                const double arithmetic_payoff = discount * arithmetic.result();
                const double geometric_payoff = discount * geometric.result();
                return arithmetic_payoff - (geometric_payoff - geometric_analytic);
            });
    }

    return monte_carlo_path_dependent<payoffs::ArithmeticAsianPayoff>(
        spot, rate, carry_yield, vol, time, monitoring_points, path_count, seed, options, strike,
        type, strike_style);
}

McResult monte_carlo_barrier(double spot, double strike, double barrier, double rate,
                              double carry_yield, double vol, double time, OptionType type,
                              BarrierDirection direction, BarrierKnock knock, double rebate,
                              int monitoring_points, std::uint64_t path_count,
                              std::uint64_t seed, McOptions options) noexcept {
    if (options.brownian_bridge) {
        return monte_carlo_barrier_brownian_bridge(spot, strike, barrier, rate, carry_yield, vol,
                                                     time, type, direction, knock, rebate,
                                                     monitoring_points, path_count, seed,
                                                     options);
    }
    return monte_carlo_path_dependent<payoffs::BarrierPayoff>(
        spot, rate, carry_yield, vol, time, monitoring_points, path_count, seed, options, strike,
        barrier, rebate, type, direction, knock);
}

McResult monte_carlo_lookback(double spot, double strike, double rate, double carry_yield,
                               double vol, double time, OptionType type, StrikeStyle style,
                               int monitoring_points, std::uint64_t path_count,
                               std::uint64_t seed, McOptions options) noexcept {
    if (style == StrikeStyle::Fixed) {
        return monte_carlo_path_dependent<payoffs::LookbackFixedPayoff>(
            spot, rate, carry_yield, vol, time, monitoring_points, path_count, seed, options,
            spot, strike, type);
    }
    return monte_carlo_path_dependent<payoffs::LookbackFloatingPayoff>(
        spot, rate, carry_yield, vol, time, monitoring_points, path_count, seed, options, spot,
        type);
}

} // namespace mcd::pricers
