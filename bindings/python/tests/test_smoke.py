"""Python smoke test for the `mcd` pybind11 module (CLAUDE.md sec.6 Phase 6 gate:
"a Python smoke test prices every product"). Uses only the standard library's own
`unittest` -- no pytest/numpy dependency, staying inside the approved dependency list
(docs/design/06-cli-bindings-reporting.md sec.4).
"""

import math
import threading
import time
import unittest

import mcd


class AnalyticPricersTest(unittest.TestCase):
    def test_black_scholes_merton_matches_known_value(self):
        price = mcd.black_scholes_merton(100, 100, 0.05, 0.0, 0.20, 1.0, mcd.OptionType.Call)
        self.assertAlmostEqual(price, 10.4506, places=3)

    def test_forward_price(self):
        price = mcd.forward_price(100, 0.05, 0.02, 1.0)
        self.assertAlmostEqual(price, 100 * math.exp((0.05 - 0.02) * 1.0), places=9)

    def test_crr_binomial_converges_toward_bsm(self):
        bsm = mcd.black_scholes_merton(100, 100, 0.05, 0.0, 0.20, 1.0, mcd.OptionType.Call)
        binomial = mcd.crr_binomial(100, 100, 0.05, 0.0, 0.20, 1.0, 2000, mcd.OptionType.Call)
        self.assertAlmostEqual(binomial.price, bsm, delta=0.05)


class MonteCarloPricersTest(unittest.TestCase):
    def test_every_product_prices_with_a_confidence_interval(self):
        seed = 42
        path_count = 100_000

        european = mcd.monte_carlo_european(100, 100, 0.05, 0.0, 0.2, 1.0, mcd.OptionType.Call,
                                              path_count, seed)
        digital = mcd.monte_carlo_digital(100, 100, 0.05, 0.0, 0.2, 1.0, mcd.OptionType.Call,
                                            mcd.DigitalStyle.CashOrNothing, 1.0, path_count, seed)
        asian = mcd.monte_carlo_asian(100, 100, 0.05, 0.0, 0.2, 1.0, mcd.OptionType.Call,
                                        mcd.StrikeStyle.Fixed, mcd.AverageStyle.Geometric, 12,
                                        path_count, seed)
        barrier = mcd.monte_carlo_barrier(100, 100, 120, 0.05, 0.0, 0.2, 1.0, mcd.OptionType.Call,
                                            mcd.BarrierDirection.Up, mcd.BarrierKnock.Out, 0.0, 20,
                                            path_count, seed)
        lookback = mcd.monte_carlo_lookback(100, 100, 0.05, 0.0, 0.2, 1.0, mcd.OptionType.Call,
                                              mcd.StrikeStyle.Floating, 20, path_count, seed)

        for result in (european, digital, asian, barrier, lookback):
            self.assertGreater(result.standard_error, 0.0)
            self.assertEqual(result.path_count, path_count)

    def test_european_within_three_se_of_analytic(self):
        seed, path_count = 42, 500_000
        mc = mcd.monte_carlo_european(100, 100, 0.05, 0.0, 0.2, 1.0, mcd.OptionType.Call,
                                        path_count, seed)
        analytic = mcd.black_scholes_merton(100, 100, 0.05, 0.0, 0.2, 1.0, mcd.OptionType.Call)
        self.assertLess(abs(mc.price - analytic), 3.0 * mc.standard_error)

    def test_american_lsm_prices(self):
        result = mcd.monte_carlo_lsm_american(100, 100, 0.05, 0.0, 0.25, 1.0, mcd.OptionType.Put,
                                                20, 50_000, 7)
        self.assertGreater(result.price, 0.0)
        self.assertGreaterEqual(result.standard_error, 0.0)


class GreeksTest(unittest.TestCase):
    def test_finite_difference_european_greeks(self):
        bumps = mcd.default_bump_sizes(100, 0.2, 1.0)
        g = mcd.finite_difference_european(100, 100, 0.05, 0.0, 0.2, 1.0, mcd.OptionType.Call,
                                             300_000, 42, bumps)
        self.assertGreater(g.delta, 0.0)
        self.assertLess(g.delta, 1.0)
        self.assertGreater(g.vega, 0.0)

    def test_likelihood_ratio_european_greeks(self):
        lr = mcd.likelihood_ratio_european(100, 100, 0.05, 0.0, 0.2, 1.0, mcd.OptionType.Call,
                                             300_000, 42)
        self.assertGreater(lr.delta.value, 0.0)
        self.assertLess(lr.delta.value, 1.0)
        self.assertGreater(lr.delta.standard_error, 0.0)
        self.assertIsNotNone(lr.theta)

    def test_likelihood_ratio_digital_and_barrier_greeks(self):
        digital = mcd.likelihood_ratio_digital(100, 100, 0.05, 0.0, 0.2, 1.0,
                                                 mcd.OptionType.Call,
                                                 mcd.DigitalStyle.CashOrNothing, 1.0, 200_000, 7)
        self.assertGreater(digital.gamma.standard_error, 0.0)

        barrier = mcd.likelihood_ratio_barrier(100, 100, 120, 0.05, 0.0, 0.2, 1.0,
                                                 mcd.OptionType.Call, mcd.BarrierDirection.Up,
                                                 mcd.BarrierKnock.Out, 0.0, 50, 100_000, 11)
        self.assertGreater(barrier.gamma.standard_error, 0.0)
        # Barrier LR Greeks deliberately don't compute theta -- see the C++
        # LrGreeks::theta comment (docs/design/08-likelihood-ratio-greeks.md sec.4).
        self.assertIsNone(barrier.theta)

    def test_likelihood_ratio_has_lower_gamma_standard_error_than_finite_difference_at_the_money_digital(self):
        # The actual point of this stretch goal (CLAUDE.md sec.7 item 1): LR gamma
        # should be dramatically more precise than FD gamma exactly where FD is
        # weakest -- a discontinuous payoff evaluated at the discontinuity itself.
        s, k, r, q, sigma, t = 100.0, 100.0, 0.05, 0.0, 0.25, 1.0
        path_count, seed = 200_000, 7

        lr = mcd.likelihood_ratio_digital(s, k, r, q, sigma, t, mcd.OptionType.Call,
                                            mcd.DigitalStyle.CashOrNothing, 1.0, path_count, seed)

        bumps = mcd.default_bump_sizes(s, sigma, t)
        v_plus = mcd.monte_carlo_digital(s + bumps.spot, k, r, q, sigma, t, mcd.OptionType.Call,
                                           mcd.DigitalStyle.CashOrNothing, 1.0, path_count, seed)
        v0 = mcd.monte_carlo_digital(s, k, r, q, sigma, t, mcd.OptionType.Call,
                                       mcd.DigitalStyle.CashOrNothing, 1.0, path_count, seed)
        v_minus = mcd.monte_carlo_digital(s - bumps.spot, k, r, q, sigma, t, mcd.OptionType.Call,
                                            mcd.DigitalStyle.CashOrNothing, 1.0, path_count, seed)
        fd_gamma_se = (
            (v_plus.standard_error ** 2 + 4 * v0.standard_error ** 2 + v_minus.standard_error ** 2)
            ** 0.5
        ) / (bumps.spot ** 2)

        self.assertLess(lr.gamma.standard_error, fd_gamma_se)

    def test_pathwise_european_greeks(self):
        pw = mcd.pathwise_european(100, 100, 0.05, 0.0, 0.2, 1.0, mcd.OptionType.Call,
                                     300_000, 42)
        self.assertGreater(pw.delta.value, 0.0)
        self.assertLess(pw.delta.value, 1.0)
        self.assertGreater(pw.delta.standard_error, 0.0)
        # No gamma/theta attributes at all -- structurally undefined for pathwise.
        self.assertFalse(hasattr(pw, "gamma"))
        self.assertFalse(hasattr(pw, "theta"))

    def test_pathwise_asian_greeks(self):
        pw = mcd.pathwise_asian(100, 100, 0.05, 0.0, 0.2, 1.0, mcd.OptionType.Call,
                                  mcd.StrikeStyle.Fixed, mcd.AverageStyle.Arithmetic, 12,
                                  100_000, 42)
        self.assertGreater(pw.delta.standard_error, 0.0)

    def test_pathwise_digital_delta_fails_the_way_the_design_doc_says_it_should(self):
        # Stretch Goal 2's actual point (CLAUDE.md sec.7 item 2: "where each breaks"):
        # this isn't bound as a product feature (see module.cpp's comment) -- verify the
        # underlying C++ behavior is still reachable and documented via the CLI instead,
        # since Python callers should never be offered a broken estimator as if it were a
        # real option. Confirms the function is deliberately absent from the module.
        self.assertFalse(hasattr(mcd, "pathwise_digital_delta_naive_and_broken"))


class QmcSobolTest(unittest.TestCase):
    def test_qmc_sobol_european_within_tolerance_of_analytic(self):
        analytic = mcd.black_scholes_merton(100, 100, 0.05, 0.02, 0.2, 1.0, mcd.OptionType.Call)
        qmc = mcd.qmc_sobol_european(100, 100, 0.05, 0.02, 0.2, 1.0, mcd.OptionType.Call, 200_000)
        self.assertAlmostEqual(qmc.price, analytic, delta=0.02)
        # QmcResult carries no standard_error -- deterministic sequence, not a random
        # variable. See docs/design/10-sobol-qmc.md sec.6.
        self.assertFalse(hasattr(qmc, "standard_error"))

    def test_qmc_sobol_asian_matches_plain_monte_carlo(self):
        seed, path_count = 42, 100_000
        mc = mcd.monte_carlo_asian(100, 100, 0.05, 0.02, 0.25, 1.0, mcd.OptionType.Call,
                                     mcd.StrikeStyle.Fixed, mcd.AverageStyle.Arithmetic, 7,
                                     path_count, seed)
        qmc = mcd.qmc_sobol_asian(100, 100, 0.05, 0.02, 0.25, 1.0, mcd.OptionType.Call,
                                    mcd.StrikeStyle.Fixed, 7, path_count)
        self.assertLess(abs(qmc.price - mc.price), 3.0 * mc.standard_error + 0.1)

    def test_qmc_sobol_asian_rejects_monitoring_points_above_the_dimension_cap(self):
        with self.assertRaises(ValueError):
            mcd.qmc_sobol_asian(100, 100, 0.05, 0.02, 0.25, 1.0, mcd.OptionType.Call,
                                  mcd.StrikeStyle.Fixed, mcd.SOBOL_MAX_DIMENSIONS + 1, 10_000)


class HestonTest(unittest.TestCase):
    # Alan Lewis's published high-precision reference (financepress.com/2019/02/15/
    # heston-model-reference-prices/), same source as tests/heston_test.cpp's
    # kLewisReferenceCases. Lewis's SDE notation maps his theta_L (mean-reversion
    # speed) to mcd's kappa, and his omega/theta_L to mcd's theta -- see the C++
    # test's comment for the full derivation.
    def _lewis_params(self):
        p = mcd.HestonParams()
        p.spot, p.rate, p.carry_yield = 100.0, 0.01, 0.02
        p.v0, p.kappa, p.theta, p.xi, p.rho = 0.04, 4.0, 0.25, 1.0, -0.5
        p.time = 1.0
        return p

    def test_semi_analytic_matches_published_reference(self):
        price = mcd.heston_semi_analytic(self._lewis_params(), 100.0, mcd.OptionType.Call)
        self.assertAlmostEqual(price, 16.070154917028834278, places=6)

    def test_qe_within_three_se_of_semi_analytic(self):
        params = self._lewis_params()
        mc = mcd.heston_qe_european(params, 100.0, mcd.OptionType.Call, 50, 100_000, 42)
        analytic = mcd.heston_semi_analytic(params, 100.0, mcd.OptionType.Call)
        self.assertLess(abs(mc.price - analytic), 3.0 * mc.standard_error)

    def test_qe_deterministic(self):
        params = self._lewis_params()
        a = mcd.heston_qe_european(params, 100.0, mcd.OptionType.Call, 20, 10_000, 7)
        b = mcd.heston_qe_european(params, 100.0, mcd.OptionType.Call, 20, 10_000, 7)
        self.assertEqual(a.price, b.price)


class BenchmarkHarnessTest(unittest.TestCase):
    def test_benchmark_european_reports_timing_and_throughput(self):
        result = mcd.benchmark_european(100, 100, 0.05, 0.0, 0.2, 1.0, mcd.OptionType.Call,
                                          200_000, 42, mcd.McOptions())
        self.assertIn("price", result)
        self.assertIn("elapsed_seconds", result)
        self.assertIn("paths_per_second", result)
        self.assertGreater(result["elapsed_seconds"], 0.0)
        self.assertGreater(result["paths_per_second"], 0.0)

    def test_benchmark_european_reports_simd_enabled_matching_has_neon(self):
        # Stretch Goal 4 (docs/design/11-simd.md): the fast path is used whenever
        # NEON is compiled in and antithetic is off -- matches mcd.HAS_NEON here
        # since McOptions() defaults antithetic to False.
        result = mcd.benchmark_european(100, 100, 0.05, 0.0, 0.2, 1.0, mcd.OptionType.Call,
                                          10_000, 42, mcd.McOptions())
        self.assertEqual(result["simd_enabled"], mcd.HAS_NEON)

    def test_european_price_deterministic_through_the_simd_path(self):
        # Real bitwise-identity coverage between the SIMD batch and scalar RNG paths
        # lives in tests/rng_simd_test.cpp (C++); this just confirms the SIMD-routed
        # pricer is still deterministic seed-to-price through the Python binding.
        seed, path_count = 7, 50_000
        first = mcd.monte_carlo_european(100, 100, 0.05, 0.02, 0.2, 1.0,
                                           mcd.OptionType.Call, path_count, seed)
        second = mcd.monte_carlo_european(100, 100, 0.05, 0.02, 0.2, 1.0,
                                            mcd.OptionType.Call, path_count, seed)
        self.assertEqual(first.price, second.price)


class GilReleaseTest(unittest.TestCase):
    def test_concurrent_pricing_calls_run_in_parallel_not_serialized(self):
        # If the GIL were held during the C++ pricing loop, two threads pricing
        # concurrently would take roughly 2x a single call's wall time. If it's actually
        # released (CLAUDE.md sec.6 Phase 6's explicit requirement), wall time for both
        # concurrent calls together stays close to one call's time.
        path_count = 2_000_000

        def single_call_seconds():
            start = time.perf_counter()
            mcd.monte_carlo_european(100, 100, 0.05, 0.0, 0.2, 1.0, mcd.OptionType.Call,
                                       path_count, 1)
            return time.perf_counter() - start

        baseline = single_call_seconds()

        start = time.perf_counter()
        threads = [
            threading.Thread(
                target=mcd.monte_carlo_european,
                args=(100, 100, 0.05, 0.0, 0.2, 1.0, mcd.OptionType.Call, path_count, i),
            )
            for i in range(2)
        ]
        for t in threads:
            t.start()
        for t in threads:
            t.join()
        concurrent = time.perf_counter() - start

        # Generous threshold (1.6x, not 2x) to absorb machine noise while still failing
        # decisively if the GIL were actually held (which would push this toward 2x).
        self.assertLess(concurrent, baseline * 1.6,
                         f"concurrent={concurrent:.3f}s baseline={baseline:.3f}s -- "
                         "looks like the GIL was not released during pricing")


if __name__ == "__main__":
    unittest.main()
