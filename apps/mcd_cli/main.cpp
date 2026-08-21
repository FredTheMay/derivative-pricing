#include "mcd/core/json.hpp"
#include "mcd/core/rng_simd.hpp"
#include "mcd/core/sobol.hpp"
#include "mcd/core/timing.hpp"
#include "mcd/core/types.hpp"
#include "mcd/greeks/finite_difference.hpp"
#include "mcd/greeks/likelihood_ratio.hpp"
#include "mcd/greeks/pathwise.hpp"
#include "mcd/pricers/analytic.hpp"
#include "mcd/pricers/binomial.hpp"
#include "mcd/pricers/heston.hpp"
#include "mcd/pricers/lsm.hpp"
#include "mcd/pricers/monte_carlo.hpp"
#include "mcd/pricers/qmc.hpp"

#include <cmath>
#include <iostream>
#include <iterator>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

using mcd::OptionType;
namespace json = mcd::json;
namespace pricers = mcd::pricers;
namespace greeks = mcd::greeks;

// The largest integer double can represent exactly. seed/path_count are round-tripped as
// JSON numbers (doubles); values above this would silently lose precision, which is
// unacceptable for a project whose whole premise is bitwise reproducibility, so it's
// rejected explicitly rather than silently truncated. See
// docs/design/06-cli-bindings-reporting.md sec.2.1.
constexpr double kMaxExactInteger = 9007199254740992.0; // 2^53

constexpr double kZ975 = 1.959963985; // two-sided 95% normal critical value

const json::Value& require(const json::Object& obj, std::string_view key) {
    const json::Value* v = json::find(obj, key);
    if (v == nullptr) {
        throw std::runtime_error("missing required field '" + std::string(key) + "'");
    }
    return *v;
}

double require_number(const json::Object& obj, std::string_view key) {
    return require(obj, key).as_number();
}

double optional_number(const json::Object& obj, std::string_view key, double default_value) {
    const json::Value* v = json::find(obj, key);
    return v != nullptr ? v->as_number() : default_value;
}

std::string require_string(const json::Object& obj, std::string_view key) {
    return require(obj, key).as_string();
}

std::string optional_string(const json::Object& obj, std::string_view key,
                             std::string default_value) {
    const json::Value* v = json::find(obj, key);
    return v != nullptr ? v->as_string() : std::move(default_value);
}

std::uint64_t require_count(const json::Object& obj, std::string_view key) {
    const double d = require_number(obj, key);
    if (d < 0.0 || d != std::floor(d) || d > kMaxExactInteger) {
        throw std::runtime_error("field '" + std::string(key) +
                                  "' must be a non-negative integer no larger than 2^53 "
                                  "(exactly representable as a JSON number)");
    }
    return static_cast<std::uint64_t>(d);
}

int require_int(const json::Object& obj, std::string_view key) {
    const double d = require_number(obj, key);
    if (d != std::floor(d) || d < static_cast<double>(INT32_MIN) ||
        d > static_cast<double>(INT32_MAX)) {
        throw std::runtime_error("field '" + std::string(key) + "' must be an integer");
    }
    return static_cast<int>(d);
}

OptionType parse_option_type(const std::string& s) {
    if (s == "call") {
        return OptionType::Call;
    }
    if (s == "put") {
        return OptionType::Put;
    }
    throw std::runtime_error("'type' must be 'call' or 'put'");
}

mcd::BarrierDirection parse_direction(const std::string& s) {
    if (s == "up") {
        return mcd::BarrierDirection::Up;
    }
    if (s == "down") {
        return mcd::BarrierDirection::Down;
    }
    throw std::runtime_error("'direction' must be 'up' or 'down'");
}

mcd::BarrierKnock parse_knock(const std::string& s) {
    if (s == "in") {
        return mcd::BarrierKnock::In;
    }
    if (s == "out") {
        return mcd::BarrierKnock::Out;
    }
    throw std::runtime_error("'knock' must be 'in' or 'out'");
}

mcd::StrikeStyle parse_strike_style(const std::string& s) {
    if (s == "fixed") {
        return mcd::StrikeStyle::Fixed;
    }
    if (s == "floating") {
        return mcd::StrikeStyle::Floating;
    }
    throw std::runtime_error("'strike_style'/'style' must be 'fixed' or 'floating'");
}

mcd::AverageStyle parse_average_style(const std::string& s) {
    if (s == "arithmetic") {
        return mcd::AverageStyle::Arithmetic;
    }
    if (s == "geometric") {
        return mcd::AverageStyle::Geometric;
    }
    throw std::runtime_error("'average_style' must be 'arithmetic' or 'geometric'");
}

mcd::DigitalStyle parse_digital_style(const std::string& s) {
    if (s == "cash_or_nothing") {
        return mcd::DigitalStyle::CashOrNothing;
    }
    if (s == "asset_or_nothing") {
        return mcd::DigitalStyle::AssetOrNothing;
    }
    throw std::runtime_error("'digital_style' must be 'cash_or_nothing' or 'asset_or_nothing'");
}

// Every Monte Carlo product's response carries all seven fields below -- this function is
// the only place an McResult becomes a JSON response, so it is not possible to build a
// response that reports a price without its confidence interval (CLAUDE.md: "a price
// reported without a confidence interval is an incomplete result").
json::Object mc_result_to_json(const pricers::McResult& r, std::uint64_t seed, double elapsed) {
    json::Object out;
    out.emplace_back("price", json::Value::from_number(r.price));
    out.emplace_back("standard_error", json::Value::from_number(r.standard_error));
    out.emplace_back("ci_95_low", json::Value::from_number(r.price - kZ975 * r.standard_error));
    out.emplace_back("ci_95_high", json::Value::from_number(r.price + kZ975 * r.standard_error));
    out.emplace_back("path_count", json::Value::from_number(static_cast<double>(r.path_count)));
    out.emplace_back("seed", json::Value::from_number(static_cast<double>(seed)));
    out.emplace_back("elapsed_seconds", json::Value::from_number(elapsed));
    const double pps = elapsed > 0.0 ? static_cast<double>(r.path_count) / elapsed : 0.0;
    out.emplace_back("paths_per_second", json::Value::from_number(pps));
    return out;
}

json::Object analytic_result_to_json(double price, double elapsed) {
    json::Object out;
    out.emplace_back("price", json::Value::from_number(price));
    out.emplace_back("elapsed_seconds", json::Value::from_number(elapsed));
    return out;
}

// Sobol-QMC results have no standard_error/CI: unlike every other Monte Carlo result in
// this project, a Sobol sequence is a deterministic low-discrepancy point set, not a
// random variable with a meaningful per-path variance -- see mcd::pricers::QmcResult's
// comment and docs/design/10-sobol-qmc.md sec.6 for how accuracy is measured instead.
// This is a deliberate, documented exception to CLAUDE.md's "a price reported without a
// confidence interval is an incomplete result" rule, surfaced via the "note" field
// rather than silently omitted, for the same structural reason pathwise_greeks_to_json
// omits gamma/theta.
json::Object qmc_result_to_json(const pricers::QmcResult& r, std::uint64_t path_count,
                                 double elapsed) {
    json::Object out;
    out.emplace_back("price", json::Value::from_number(r.price));
    out.emplace_back("path_count", json::Value::from_number(static_cast<double>(path_count)));
    out.emplace_back("elapsed_seconds", json::Value::from_number(elapsed));
    const double pps = elapsed > 0.0 ? static_cast<double>(path_count) / elapsed : 0.0;
    out.emplace_back("paths_per_second", json::Value::from_number(pps));
    out.emplace_back("note",
                      json::Value::from_string(
                          "Sobol QMC is deterministic: no standard_error/confidence interval "
                          "is reported. See docs/design/10-sobol-qmc.md sec.6."));
    return out;
}

// Every LR/pathwise Greek carries its own standard error, same statistical treatment as
// every other Monte Carlo result in this project (docs/design/08-likelihood-ratio-
// greeks.md sec.5, docs/design/09-pathwise-greeks.md sec.4). theta is omitted entirely
// (not reported as a misleading zero) for products/methods where it isn't computed.
template <typename GreekResult>
void append_greek_result(json::Object& out, const char* name, const GreekResult& r) {
    out.emplace_back(name, json::Value::from_number(r.value));
    out.emplace_back(std::string(name) + "_se", json::Value::from_number(r.standard_error));
}

json::Object lr_greeks_to_json(const greeks::LrGreeks& g, double elapsed) {
    json::Object out;
    append_greek_result(out, "delta", g.delta);
    append_greek_result(out, "gamma", g.gamma);
    append_greek_result(out, "vega", g.vega);
    append_greek_result(out, "rho", g.rho);
    if (g.theta.has_value()) {
        append_greek_result(out, "theta", *g.theta);
    }
    out.emplace_back("elapsed_seconds", json::Value::from_number(elapsed));
    return out;
}

// No gamma/theta fields at all (not omitted-when-empty, genuinely absent from the type)
// -- pathwise gamma is structurally undefined for every product; see
// mcd::greeks::PathwiseGreeks's comment.
json::Object pathwise_greeks_to_json(const greeks::PathwiseGreeks& g, double elapsed) {
    json::Object out;
    append_greek_result(out, "delta", g.delta);
    append_greek_result(out, "vega", g.vega);
    append_greek_result(out, "rho", g.rho);
    out.emplace_back("elapsed_seconds", json::Value::from_number(elapsed));
    return out;
}

json::Object handle_request(const json::Object& req) {
    const std::string product = require_string(req, "product");
    const std::string request_kind = optional_string(req, "request", "price");

    if (product == "forward") {
        const double spot = require_number(req, "spot");
        const double rate = require_number(req, "rate");
        const double carry_yield = require_number(req, "carry_yield");
        const double time = require_number(req, "time");
        const auto timed = mcd::time_call(
            [&] { return pricers::forward_price(spot, rate, carry_yield, time); });
        return analytic_result_to_json(timed.value, timed.elapsed_seconds);
    }

    if (product == "binomial_european" || product == "binomial_american") {
        const double spot = require_number(req, "spot");
        const double strike = require_number(req, "strike");
        const double rate = require_number(req, "rate");
        const double carry_yield = require_number(req, "carry_yield");
        const double vol = require_number(req, "vol");
        const double time = require_number(req, "time");
        const OptionType type = parse_option_type(require_string(req, "type"));
        const int steps = require_int(req, "steps");

        if (product == "binomial_american") {
            const auto timed = mcd::time_call([&] {
                return pricers::crr_binomial_american(spot, strike, rate, carry_yield, vol, time,
                                                        steps, type);
            });
            return analytic_result_to_json(timed.value, timed.elapsed_seconds);
        }
        const auto timed = mcd::time_call([&] {
            return pricers::crr_binomial(spot, strike, rate, carry_yield, vol, time, steps, type);
        });
        json::Object out = analytic_result_to_json(timed.value.price, timed.elapsed_seconds);
        out.emplace_back("risk_neutral_probability",
                          json::Value::from_number(timed.value.risk_neutral_probability));
        out.emplace_back("up_factor", json::Value::from_number(timed.value.up_factor));
        out.emplace_back("down_factor", json::Value::from_number(timed.value.down_factor));
        return out;
    }

    if (product == "european") {
        const double spot = require_number(req, "spot");
        const double strike = require_number(req, "strike");
        const double rate = require_number(req, "rate");
        const double carry_yield = require_number(req, "carry_yield");
        const double vol = require_number(req, "vol");
        const double time = require_number(req, "time");
        const OptionType type = parse_option_type(require_string(req, "type"));

        if (request_kind == "greeks") {
            const std::uint64_t path_count = require_count(req, "path_count");
            const std::uint64_t seed = require_count(req, "seed");
            const auto bumps = greeks::default_bump_sizes(spot, vol, time);
            const auto timed = mcd::time_call([&] {
                return greeks::finite_difference_european(spot, strike, rate, carry_yield, vol,
                                                            time, type, path_count, seed, bumps);
            });
            json::Object out;
            out.emplace_back("delta", json::Value::from_number(timed.value.delta));
            out.emplace_back("gamma", json::Value::from_number(timed.value.gamma));
            out.emplace_back("vega", json::Value::from_number(timed.value.vega));
            out.emplace_back("theta", json::Value::from_number(timed.value.theta));
            out.emplace_back("rho", json::Value::from_number(timed.value.rho));
            out.emplace_back("elapsed_seconds", json::Value::from_number(timed.elapsed_seconds));
            return out;
        }

        if (request_kind == "lr_greeks") {
            const std::uint64_t path_count = require_count(req, "path_count");
            const std::uint64_t seed = require_count(req, "seed");
            const auto timed = mcd::time_call([&] {
                return greeks::likelihood_ratio_european(spot, strike, rate, carry_yield, vol,
                                                          time, type, path_count, seed);
            });
            return lr_greeks_to_json(timed.value, timed.elapsed_seconds);
        }

        if (request_kind == "pathwise_greeks") {
            const std::uint64_t path_count = require_count(req, "path_count");
            const std::uint64_t seed = require_count(req, "seed");
            const auto timed = mcd::time_call([&] {
                return greeks::pathwise_european(spot, strike, rate, carry_yield, vol, time,
                                                  type, path_count, seed);
            });
            return pathwise_greeks_to_json(timed.value, timed.elapsed_seconds);
        }

        if (request_kind == "qmc_sobol") {
            const std::uint64_t path_count = require_count(req, "path_count");
            const auto timed = mcd::time_call([&] {
                return pricers::qmc_sobol_european(spot, strike, rate, carry_yield, vol, time,
                                                    type, path_count);
            });
            return qmc_result_to_json(timed.value, path_count, timed.elapsed_seconds);
        }

        const std::uint64_t path_count = require_count(req, "path_count");
        const std::uint64_t seed = require_count(req, "seed");
        const auto timed = mcd::time_call([&] {
            return pricers::monte_carlo_european(spot, strike, rate, carry_yield, vol, time, type,
                                                  path_count, seed);
        });
        json::Object out = mc_result_to_json(timed.value, seed, timed.elapsed_seconds);
        // Stretch Goal 4 (docs/design/11-simd.md): European and digital are the two
        // products that funnel through monte_carlo_terminal's SIMD fast path, used
        // automatically whenever NEON is available and antithetic is off -- true for
        // this zero-options call. Reported for transparency into which code path
        // produced a given throughput number, per your explicit choice on this stretch
        // goal's exposure.
        out.emplace_back("simd_enabled", json::Value::from_bool(mcd::kHasNeon));
        return out;
    }

    if (product == "digital") {
        const double spot = require_number(req, "spot");
        const double strike = require_number(req, "strike");
        const double rate = require_number(req, "rate");
        const double carry_yield = require_number(req, "carry_yield");
        const double vol = require_number(req, "vol");
        const double time = require_number(req, "time");
        const OptionType type = parse_option_type(require_string(req, "type"));
        const auto style = parse_digital_style(require_string(req, "digital_style"));
        const double cash_amount = optional_number(req, "cash_amount", 1.0);
        const std::uint64_t path_count = require_count(req, "path_count");
        const std::uint64_t seed = require_count(req, "seed");

        if (request_kind == "lr_greeks") {
            const auto timed = mcd::time_call([&] {
                return greeks::likelihood_ratio_digital(spot, strike, rate, carry_yield, vol,
                                                         time, type, style, cash_amount,
                                                         path_count, seed);
            });
            return lr_greeks_to_json(timed.value, timed.elapsed_seconds);
        }

        const auto timed = mcd::time_call([&] {
            return pricers::monte_carlo_digital(spot, strike, rate, carry_yield, vol, time, type,
                                                 style, cash_amount, path_count, seed);
        });
        json::Object out = mc_result_to_json(timed.value, seed, timed.elapsed_seconds);
        out.emplace_back("simd_enabled", json::Value::from_bool(mcd::kHasNeon));
        return out;
    }

    if (product == "asian") {
        const double spot = require_number(req, "spot");
        const double strike = require_number(req, "strike");
        const double rate = require_number(req, "rate");
        const double carry_yield = require_number(req, "carry_yield");
        const double vol = require_number(req, "vol");
        const double time = require_number(req, "time");
        const OptionType type = parse_option_type(require_string(req, "type"));
        const auto strike_style = parse_strike_style(require_string(req, "strike_style"));
        const auto average_style = parse_average_style(require_string(req, "average_style"));
        const int monitoring_points = require_int(req, "monitoring_points");
        const std::uint64_t path_count = require_count(req, "path_count");

        if (request_kind == "qmc_sobol") {
            if (average_style != mcd::AverageStyle::Arithmetic) {
                throw std::runtime_error("'qmc_sobol' asian only supports 'arithmetic' "
                                          "average_style (docs/design/10-sobol-qmc.md sec.4)");
            }
            if (monitoring_points < 1 ||
                monitoring_points > static_cast<int>(mcd::kSobolMaxDimensions)) {
                throw std::runtime_error(
                    "'monitoring_points' for 'qmc_sobol' asian must be between 1 and " +
                    std::to_string(mcd::kSobolMaxDimensions) +
                    " (docs/design/10-sobol-qmc.md sec.2/4: the from-scratch-verified Sobol "
                    "dimension budget)");
            }
            const auto timed = mcd::time_call([&] {
                return pricers::qmc_sobol_asian(spot, strike, rate, carry_yield, vol, time, type,
                                                 strike_style, monitoring_points, path_count);
            });
            return qmc_result_to_json(timed.value, path_count, timed.elapsed_seconds);
        }

        const std::uint64_t seed = require_count(req, "seed");

        if (request_kind == "pathwise_greeks") {
            const auto timed = mcd::time_call([&] {
                return greeks::pathwise_asian(spot, strike, rate, carry_yield, vol, time, type,
                                               strike_style, average_style, monitoring_points,
                                               path_count, seed);
            });
            return pathwise_greeks_to_json(timed.value, timed.elapsed_seconds);
        }

        const auto timed = mcd::time_call([&] {
            return pricers::monte_carlo_asian(spot, strike, rate, carry_yield, vol, time, type,
                                               strike_style, average_style, monitoring_points,
                                               path_count, seed);
        });
        return mc_result_to_json(timed.value, seed, timed.elapsed_seconds);
    }

    if (product == "barrier") {
        const double spot = require_number(req, "spot");
        const double strike = require_number(req, "strike");
        const double barrier = require_number(req, "barrier");
        const double rate = require_number(req, "rate");
        const double carry_yield = require_number(req, "carry_yield");
        const double vol = require_number(req, "vol");
        const double time = require_number(req, "time");
        const OptionType type = parse_option_type(require_string(req, "type"));
        const auto direction = parse_direction(require_string(req, "direction"));
        const auto knock = parse_knock(require_string(req, "knock"));
        const double rebate = optional_number(req, "rebate", 0.0);
        const int monitoring_points = require_int(req, "monitoring_points");
        const std::uint64_t path_count = require_count(req, "path_count");
        const std::uint64_t seed = require_count(req, "seed");

        if (request_kind == "lr_greeks") {
            const auto timed = mcd::time_call([&] {
                return greeks::likelihood_ratio_barrier(spot, strike, barrier, rate, carry_yield,
                                                         vol, time, type, direction, knock,
                                                         rebate, monitoring_points, path_count,
                                                         seed);
            });
            return lr_greeks_to_json(timed.value, timed.elapsed_seconds);
        }

        const auto timed = mcd::time_call([&] {
            return pricers::monte_carlo_barrier(spot, strike, barrier, rate, carry_yield, vol,
                                                 time, type, direction, knock, rebate,
                                                 monitoring_points, path_count, seed);
        });
        return mc_result_to_json(timed.value, seed, timed.elapsed_seconds);
    }

    if (product == "lookback") {
        const double spot = require_number(req, "spot");
        const double strike = optional_number(req, "strike", 0.0);
        const double rate = require_number(req, "rate");
        const double carry_yield = require_number(req, "carry_yield");
        const double vol = require_number(req, "vol");
        const double time = require_number(req, "time");
        const OptionType type = parse_option_type(require_string(req, "type"));
        const auto style = parse_strike_style(require_string(req, "style"));
        const int monitoring_points = require_int(req, "monitoring_points");
        const std::uint64_t path_count = require_count(req, "path_count");
        const std::uint64_t seed = require_count(req, "seed");
        const auto timed = mcd::time_call([&] {
            return pricers::monte_carlo_lookback(spot, strike, rate, carry_yield, vol, time, type,
                                                  style, monitoring_points, path_count, seed);
        });
        return mc_result_to_json(timed.value, seed, timed.elapsed_seconds);
    }

    if (product == "american") {
        const double spot = require_number(req, "spot");
        const double strike = require_number(req, "strike");
        const double rate = require_number(req, "rate");
        const double carry_yield = require_number(req, "carry_yield");
        const double vol = require_number(req, "vol");
        const double time = require_number(req, "time");
        const OptionType type = parse_option_type(require_string(req, "type"));
        const int monitoring_points = require_int(req, "monitoring_points");
        const std::uint64_t path_count = require_count(req, "path_count");
        const std::uint64_t seed = require_count(req, "seed");
        const auto timed = mcd::time_call([&] {
            return pricers::monte_carlo_lsm_american(spot, strike, rate, carry_yield, vol, time,
                                                       type, monitoring_points, path_count, seed);
        });
        pricers::McResult as_mc{.price = timed.value.price,
                                 .standard_error = timed.value.standard_error,
                                 .path_count = path_count};
        return mc_result_to_json(as_mc, seed, timed.elapsed_seconds);
    }

    if (product == "heston") {
        const double spot = require_number(req, "spot");
        const double strike = require_number(req, "strike");
        const double rate = require_number(req, "rate");
        const double carry_yield = require_number(req, "carry_yield");
        const double v0 = require_number(req, "v0");
        const double kappa = require_number(req, "kappa");
        const double theta = require_number(req, "theta");
        const double xi = require_number(req, "xi");
        const double rho = require_number(req, "rho");
        const double time = require_number(req, "time");
        const OptionType type = parse_option_type(require_string(req, "type"));
        const mcd::models::HestonParams params{.spot = spot, .rate = rate,
                                                 .carry_yield = carry_yield, .v0 = v0,
                                                 .kappa = kappa, .theta = theta, .xi = xi,
                                                 .rho = rho, .time = time};

        if (request_kind == "semi_analytic") {
            const auto timed = mcd::time_call(
                [&] { return pricers::heston_semi_analytic(params, strike, type); });
            return analytic_result_to_json(timed.value, timed.elapsed_seconds);
        }

        const int monitoring_points = require_int(req, "monitoring_points");
        const std::uint64_t path_count = require_count(req, "path_count");
        const std::uint64_t seed = require_count(req, "seed");
        const auto timed = mcd::time_call([&] {
            return pricers::heston_qe_european(params, strike, type, monitoring_points,
                                                path_count, seed);
        });
        pricers::McResult as_mc{.price = timed.value.price,
                                 .standard_error = timed.value.standard_error,
                                 .path_count = timed.value.path_count};
        return mc_result_to_json(as_mc, seed, timed.elapsed_seconds);
    }

    throw std::runtime_error("unknown 'product': '" + product + "'");
}

} // namespace

int main() {
    const std::string input((std::istreambuf_iterator<char>(std::cin)),
                             std::istreambuf_iterator<char>());
    try {
        const json::Object request = json::parse_object(input);
        const json::Object response = handle_request(request);
        std::cout << json::serialize(response) << "\n";
        return 0;
    } catch (const std::exception& e) {
        json::Object error_obj;
        error_obj.emplace_back("error", json::Value::from_string(e.what()));
        std::cout << json::serialize(error_obj) << "\n";
        return 1;
    }
}
