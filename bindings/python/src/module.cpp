#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include "mcd/core/timing.hpp"
#include "mcd/core/types.hpp"
#include "mcd/greeks/finite_difference.hpp"
#include "mcd/greeks/likelihood_ratio.hpp"
#include "mcd/greeks/pathwise.hpp"
#include "mcd/pricers/analytic.hpp"
#include "mcd/pricers/binomial.hpp"
#include "mcd/core/sobol.hpp"
#include "mcd/pricers/lsm.hpp"
#include "mcd/pricers/monte_carlo.hpp"
#include "mcd/pricers/qmc.hpp"

namespace py = pybind11;
using namespace mcd;
namespace pricers = mcd::pricers;
namespace greeks = mcd::greeks;

namespace {

// "The benchmark harness" CLAUDE.md sec.6 Phase 6 asks the bindings to expose: the same
// mcd::time_call primitive mcd_cli uses to report elapsed_seconds/paths_per_second (see
// docs/design/06-cli-bindings-reporting.md sec.3.1/sec.4), wrapped around the flagship
// streaming product (European) rather than duplicated per product.
py::dict benchmark_european(double spot, double strike, double rate, double carry_yield,
                             double vol, double time, OptionType type,
                             std::uint64_t path_count, std::uint64_t seed, pricers::McOptions options) {
    // Deliberately not py::call_guard<py::gil_scoped_release> on this whole function (unlike
    // every other binding below): this function builds a py::dict return value itself, which
    // needs the GIL. call_guard would hold the GIL released for the *entire* function body,
    // including that dict construction -- corrupting the interpreter (segfault, caught by
    // bindings/python/tests/test_smoke.py's BenchmarkHarnessTest during this phase's
    // implementation). The GIL is released only around the actual pricing work instead.
    mcd::TimingResult<pricers::McResult> timed{};
    {
        py::gil_scoped_release release;
        timed = mcd::time_call([&] {
            return pricers::monte_carlo_european(spot, strike, rate, carry_yield, vol, time,
                                                  type, path_count, seed, options);
        });
    }
    py::dict out;
    out["price"] = timed.value.price;
    out["standard_error"] = timed.value.standard_error;
    out["path_count"] = timed.value.path_count;
    out["elapsed_seconds"] = timed.elapsed_seconds;
    out["paths_per_second"] =
        timed.elapsed_seconds > 0.0
            ? static_cast<double>(timed.value.path_count) / timed.elapsed_seconds
            : 0.0;
    return out;
}

} // namespace

PYBIND11_MODULE(mcd, m) {
    m.doc() = "Python bindings for the mcd Monte Carlo derivatives pricing engine";

    py::enum_<OptionType>(m, "OptionType")
        .value("Call", OptionType::Call)
        .value("Put", OptionType::Put);
    py::enum_<BarrierDirection>(m, "BarrierDirection")
        .value("Up", BarrierDirection::Up)
        .value("Down", BarrierDirection::Down);
    py::enum_<BarrierKnock>(m, "BarrierKnock")
        .value("In", BarrierKnock::In)
        .value("Out", BarrierKnock::Out);
    py::enum_<StrikeStyle>(m, "StrikeStyle")
        .value("Fixed", StrikeStyle::Fixed)
        .value("Floating", StrikeStyle::Floating);
    py::enum_<DigitalStyle>(m, "DigitalStyle")
        .value("CashOrNothing", DigitalStyle::CashOrNothing)
        .value("AssetOrNothing", DigitalStyle::AssetOrNothing);
    py::enum_<AverageStyle>(m, "AverageStyle")
        .value("Arithmetic", AverageStyle::Arithmetic)
        .value("Geometric", AverageStyle::Geometric);

    py::class_<pricers::McResult>(m, "McResult")
        .def_readonly("price", &pricers::McResult::price)
        .def_readonly("standard_error", &pricers::McResult::standard_error)
        .def_readonly("path_count", &pricers::McResult::path_count);

    py::class_<pricers::McOptions>(m, "McOptions")
        .def(py::init<>())
        .def_readwrite("antithetic", &pricers::McOptions::antithetic)
        .def_readwrite("control_variate", &pricers::McOptions::control_variate)
        .def_readwrite("brownian_bridge", &pricers::McOptions::brownian_bridge)
        .def_readwrite("num_threads", &pricers::McOptions::num_threads);

    py::class_<pricers::BinomialResult>(m, "BinomialResult")
        .def_readonly("price", &pricers::BinomialResult::price)
        .def_readonly("risk_neutral_probability",
                       &pricers::BinomialResult::risk_neutral_probability)
        .def_readonly("up_factor", &pricers::BinomialResult::up_factor)
        .def_readonly("down_factor", &pricers::BinomialResult::down_factor);

    py::class_<pricers::LsmResult>(m, "LsmResult")
        .def_readonly("price", &pricers::LsmResult::price)
        .def_readonly("standard_error", &pricers::LsmResult::standard_error);

    py::class_<greeks::EuropeanGreeks>(m, "EuropeanGreeks")
        .def_readonly("delta", &greeks::EuropeanGreeks::delta)
        .def_readonly("gamma", &greeks::EuropeanGreeks::gamma)
        .def_readonly("vega", &greeks::EuropeanGreeks::vega)
        .def_readonly("theta", &greeks::EuropeanGreeks::theta)
        .def_readonly("rho", &greeks::EuropeanGreeks::rho);

    py::class_<greeks::BumpSizes>(m, "BumpSizes")
        .def(py::init<>())
        .def_readwrite("spot", &greeks::BumpSizes::spot)
        .def_readwrite("vol", &greeks::BumpSizes::vol)
        .def_readwrite("rate", &greeks::BumpSizes::rate)
        .def_readwrite("time", &greeks::BumpSizes::time);

    py::class_<greeks::LrGreeksResult>(m, "LrGreeksResult")
        .def_readonly("value", &greeks::LrGreeksResult::value)
        .def_readonly("standard_error", &greeks::LrGreeksResult::standard_error);

    py::class_<greeks::LrGreeks>(m, "LrGreeks")
        .def_readonly("delta", &greeks::LrGreeks::delta)
        .def_readonly("gamma", &greeks::LrGreeks::gamma)
        .def_readonly("vega", &greeks::LrGreeks::vega)
        .def_readonly("rho", &greeks::LrGreeks::rho)
        // None for barrier (theta deliberately not computed there -- see the C++
        // LrGreeks::theta comment), a real LrGreeksResult for European/digital.
        .def_readonly("theta", &greeks::LrGreeks::theta);

    py::class_<greeks::PathwiseGreeksResult>(m, "PathwiseGreeksResult")
        .def_readonly("value", &greeks::PathwiseGreeksResult::value)
        .def_readonly("standard_error", &greeks::PathwiseGreeksResult::standard_error);

    // No gamma/theta attributes at all -- structurally undefined for pathwise on every
    // product, not "not computed for this one" (see PathwiseGreeks's C++ comment).
    py::class_<greeks::PathwiseGreeks>(m, "PathwiseGreeks")
        .def_readonly("delta", &greeks::PathwiseGreeks::delta)
        .def_readonly("vega", &greeks::PathwiseGreeks::vega)
        .def_readonly("rho", &greeks::PathwiseGreeks::rho);

    // No standard_error field -- Sobol QMC is deterministic, not a random variable in
    // the sense plain Monte Carlo's paths are. See mcd::pricers::QmcResult's C++
    // comment and docs/design/10-sobol-qmc.md sec.6.
    py::class_<pricers::QmcResult>(m, "QmcResult").def_readonly("price", &pricers::QmcResult::price);

    m.attr("SOBOL_MAX_DIMENSIONS") = kSobolMaxDimensions;

    // Analytic pricers -- deterministic, GIL release is harmless but not load-bearing
    // (these are fast); released anyway for a uniform calling convention.
    m.def("black_scholes_merton", &pricers::black_scholes_merton, py::call_guard<py::gil_scoped_release>());
    m.def("black76", &pricers::black76, py::call_guard<py::gil_scoped_release>());
    m.def("kemna_vorst", &pricers::kemna_vorst, py::call_guard<py::gil_scoped_release>());
    m.def("reiner_rubinstein", &pricers::reiner_rubinstein, py::call_guard<py::gil_scoped_release>(),
          py::arg("spot"), py::arg("strike"), py::arg("barrier"), py::arg("rate"),
          py::arg("carry_yield"), py::arg("vol"), py::arg("time"), py::arg("type"),
          py::arg("direction"), py::arg("knock"), py::arg("rebate") = 0.0);
    m.def("lookback_fixed_strike", &pricers::lookback_fixed_strike, py::call_guard<py::gil_scoped_release>());
    m.def("lookback_floating_strike", &pricers::lookback_floating_strike, py::call_guard<py::gil_scoped_release>());
    m.def("digital", &pricers::digital, py::call_guard<py::gil_scoped_release>(),
          py::arg("spot"), py::arg("strike"), py::arg("rate"), py::arg("carry_yield"),
          py::arg("vol"), py::arg("time"), py::arg("type"), py::arg("style"),
          py::arg("cash_amount") = 1.0);
    m.def("forward_price", &pricers::forward_price, py::call_guard<py::gil_scoped_release>());
    m.def("forward_value", &pricers::forward_value, py::call_guard<py::gil_scoped_release>());
    m.def("futures_mark_to_market", &pricers::futures_mark_to_market, py::call_guard<py::gil_scoped_release>());

    // Binomial pricers.
    m.def("crr_binomial", &pricers::crr_binomial, py::call_guard<py::gil_scoped_release>());
    m.def("crr_binomial_american", &pricers::crr_binomial_american, py::call_guard<py::gil_scoped_release>());

    // Monte Carlo pricers -- released GIL is load-bearing here (CLAUDE.md sec.6 Phase 6):
    // these are the calls that can run for seconds at high path counts, and a caller running
    // several from separate Python threads needs the C++ work to actually run concurrently.
    m.def("monte_carlo_european",
          static_cast<pricers::McResult (*)(double, double, double, double, double, double,
                                             OptionType, std::uint64_t, std::uint64_t, pricers::McOptions)>(
              &pricers::monte_carlo_european),
          py::call_guard<py::gil_scoped_release>(), py::arg("spot"), py::arg("strike"),
          py::arg("rate"), py::arg("carry_yield"), py::arg("vol"), py::arg("time"),
          py::arg("type"), py::arg("path_count"), py::arg("seed"),
          py::arg("options") = pricers::McOptions{});
    m.def("monte_carlo_digital", &pricers::monte_carlo_digital, py::call_guard<py::gil_scoped_release>(),
          py::arg("spot"), py::arg("strike"), py::arg("rate"), py::arg("carry_yield"),
          py::arg("vol"), py::arg("time"), py::arg("type"), py::arg("style"),
          py::arg("cash_amount"), py::arg("path_count"), py::arg("seed"),
          py::arg("options") = pricers::McOptions{});
    m.def("monte_carlo_asian", &pricers::monte_carlo_asian, py::call_guard<py::gil_scoped_release>(),
          py::arg("spot"), py::arg("strike"), py::arg("rate"), py::arg("carry_yield"),
          py::arg("vol"), py::arg("time"), py::arg("type"), py::arg("strike_style"),
          py::arg("average_style"), py::arg("monitoring_points"), py::arg("path_count"),
          py::arg("seed"), py::arg("options") = pricers::McOptions{});
    m.def("monte_carlo_barrier", &pricers::monte_carlo_barrier, py::call_guard<py::gil_scoped_release>(),
          py::arg("spot"), py::arg("strike"), py::arg("barrier"), py::arg("rate"),
          py::arg("carry_yield"), py::arg("vol"), py::arg("time"), py::arg("type"),
          py::arg("direction"), py::arg("knock"), py::arg("rebate"),
          py::arg("monitoring_points"), py::arg("path_count"), py::arg("seed"),
          py::arg("options") = pricers::McOptions{});
    m.def("monte_carlo_lookback", &pricers::monte_carlo_lookback, py::call_guard<py::gil_scoped_release>(),
          py::arg("spot"), py::arg("strike"), py::arg("rate"), py::arg("carry_yield"),
          py::arg("vol"), py::arg("time"), py::arg("type"), py::arg("style"),
          py::arg("monitoring_points"), py::arg("path_count"), py::arg("seed"),
          py::arg("options") = pricers::McOptions{});

    // American (Longstaff-Schwartz).
    m.def("monte_carlo_lsm_american", &pricers::monte_carlo_lsm_american,
          py::call_guard<py::gil_scoped_release>());

    // Finite-difference Greeks.
    m.def("default_bump_sizes", &greeks::default_bump_sizes, py::call_guard<py::gil_scoped_release>());
    m.def("finite_difference_european", &greeks::finite_difference_european,
          py::call_guard<py::gil_scoped_release>());

    // Likelihood-ratio Greeks (Stretch Goal 1, docs/design/08-likelihood-ratio-greeks.md).
    m.def("likelihood_ratio_european", &greeks::likelihood_ratio_european,
          py::call_guard<py::gil_scoped_release>());
    m.def("likelihood_ratio_digital", &greeks::likelihood_ratio_digital,
          py::call_guard<py::gil_scoped_release>());
    m.def("likelihood_ratio_barrier", &greeks::likelihood_ratio_barrier,
          py::call_guard<py::gil_scoped_release>());

    // Pathwise-derivative Greeks (Stretch Goal 2, docs/design/09-pathwise-greeks.md).
    // pathwise_digital_delta_naive_and_broken deliberately not bound -- a documented
    // failure mode exercised by the C++ test suite, not a product feature.
    m.def("pathwise_european", &greeks::pathwise_european,
          py::call_guard<py::gil_scoped_release>());
    m.def("pathwise_asian", &greeks::pathwise_asian,
          py::call_guard<py::gil_scoped_release>());

    // Sobol QMC (Stretch Goal 3, docs/design/10-sobol-qmc.md). qmc_sobol_asian validates
    // monitoring_points against SOBOL_MAX_DIMENSIONS here -- unlike every other pricer in
    // this module, whose validation lives only at the mcd_cli/AWS request layer, this
    // check is duplicated at the binding since Python callers do not go through mcd_cli.
    m.def("qmc_sobol_european", &pricers::qmc_sobol_european, py::call_guard<py::gil_scoped_release>());
    m.def(
        "qmc_sobol_asian",
        [](double spot, double strike, double rate, double carry_yield, double vol, double time,
           OptionType type, StrikeStyle strike_style, int monitoring_points,
           std::uint64_t path_count) {
            if (monitoring_points < 1 ||
                monitoring_points > static_cast<int>(kSobolMaxDimensions)) {
                throw py::value_error("monitoring_points must be between 1 and " +
                                       std::to_string(kSobolMaxDimensions));
            }
            py::gil_scoped_release release;
            return pricers::qmc_sobol_asian(spot, strike, rate, carry_yield, vol, time, type,
                                             strike_style, monitoring_points, path_count);
        },
        py::arg("spot"), py::arg("strike"), py::arg("rate"), py::arg("carry_yield"),
        py::arg("vol"), py::arg("time"), py::arg("type"), py::arg("strike_style"),
        py::arg("monitoring_points"), py::arg("path_count"));

    // Benchmark harness. No call_guard here -- see the comment on benchmark_european's
    // definition for why (it manages the GIL itself, around only the pricing call).
    m.def("benchmark_european", &benchmark_european,
          py::arg("spot"), py::arg("strike"), py::arg("rate"), py::arg("carry_yield"),
          py::arg("vol"), py::arg("time"), py::arg("type"), py::arg("path_count"),
          py::arg("seed"), py::arg("options") = pricers::McOptions{});
}
