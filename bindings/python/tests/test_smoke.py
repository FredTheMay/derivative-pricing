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


class BenchmarkHarnessTest(unittest.TestCase):
    def test_benchmark_european_reports_timing_and_throughput(self):
        result = mcd.benchmark_european(100, 100, 0.05, 0.0, 0.2, 1.0, mcd.OptionType.Call,
                                          200_000, 42, mcd.McOptions())
        self.assertIn("price", result)
        self.assertIn("elapsed_seconds", result)
        self.assertIn("paths_per_second", result)
        self.assertGreater(result["elapsed_seconds"], 0.0)
        self.assertGreater(result["paths_per_second"], 0.0)


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
