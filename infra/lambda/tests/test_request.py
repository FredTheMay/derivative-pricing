"""Pure-Python unit tests for infra/lambda/request.py's validation and dispatch logic --
no AWS needed, run in CI (docs/design/07-aws-demo.md sec.6). Needs the `mcd` extension
built first (same as bindings/python/tests/test_smoke.py, Phase 6).
"""

import os
import sys
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))

from request import MAX_PATH_COUNT, RequestError, handle_request  # noqa: E402


class ValidationTest(unittest.TestCase):
    def test_missing_product_raises(self):
        with self.assertRaises(RequestError):
            handle_request({})

    def test_unknown_product_raises(self):
        with self.assertRaises(RequestError):
            handle_request({"product": "not_a_real_product"})

    def test_non_dict_request_raises(self):
        with self.assertRaises(RequestError):
            handle_request("not a dict")
        with self.assertRaises(RequestError):
            handle_request([1, 2, 3])

    def test_missing_required_field_raises(self):
        with self.assertRaises(RequestError):
            handle_request({"product": "european"})

    def test_path_count_above_cap_is_rejected(self):
        with self.assertRaises(RequestError):
            handle_request({
                "product": "european", "spot": 100, "strike": 100, "rate": 0.05,
                "carry_yield": 0.0, "vol": 0.2, "time": 1.0, "type": "call",
                "path_count": MAX_PATH_COUNT + 1, "seed": 1,
            })

    def test_negative_path_count_is_rejected(self):
        with self.assertRaises(RequestError):
            handle_request({
                "product": "european", "spot": 100, "strike": 100, "rate": 0.05,
                "carry_yield": 0.0, "vol": 0.2, "time": 1.0, "type": "call",
                "path_count": -1, "seed": 1,
            })

    def test_invalid_option_type_is_rejected(self):
        with self.assertRaises(RequestError):
            handle_request({
                "product": "european", "spot": 100, "strike": 100, "rate": 0.05,
                "carry_yield": 0.0, "vol": 0.2, "time": 1.0, "type": "not_call_or_put",
                "path_count": 1000, "seed": 1,
            })

    def test_binomial_steps_above_cap_is_rejected(self):
        with self.assertRaises(RequestError):
            handle_request({
                "product": "binomial_european", "spot": 100, "strike": 100, "rate": 0.05,
                "carry_yield": 0.0, "vol": 0.2, "time": 1.0, "type": "call", "steps": 50_000,
            })


class DispatchTest(unittest.TestCase):
    def test_forward_returns_price_without_confidence_interval(self):
        result = handle_request({
            "product": "forward", "spot": 100, "rate": 0.05, "carry_yield": 0.02, "time": 1.0,
        })
        self.assertIn("price", result)
        self.assertNotIn("standard_error", result)

    def test_european_returns_full_schema(self):
        result = handle_request({
            "product": "european", "spot": 100, "strike": 100, "rate": 0.05,
            "carry_yield": 0.0, "vol": 0.2, "time": 1.0, "type": "call",
            "path_count": 50_000, "seed": 1,
        })
        for field in ("price", "standard_error", "ci_95_low", "ci_95_high", "path_count",
                      "seed", "elapsed_seconds", "paths_per_second"):
            self.assertIn(field, result)

    def test_european_greeks_returns_all_five(self):
        result = handle_request({
            "product": "european", "request": "greeks", "spot": 100, "strike": 100,
            "rate": 0.05, "carry_yield": 0.0, "vol": 0.2, "time": 1.0, "type": "call",
            "path_count": 50_000, "seed": 1,
        })
        for field in ("delta", "gamma", "vega", "theta", "rho"):
            self.assertIn(field, result)

    def test_binomial_european_includes_tree_parameters(self):
        result = handle_request({
            "product": "binomial_european", "spot": 100, "strike": 100, "rate": 0.05,
            "carry_yield": 0.0, "vol": 0.2, "time": 1.0, "type": "call", "steps": 200,
        })
        for field in ("price", "risk_neutral_probability", "up_factor", "down_factor"):
            self.assertIn(field, result)

    def test_american_returns_full_schema(self):
        result = handle_request({
            "product": "american", "spot": 100, "strike": 100, "rate": 0.05,
            "carry_yield": 0.0, "vol": 0.25, "time": 1.0, "type": "put",
            "monitoring_points": 10, "path_count": 20_000, "seed": 7,
        })
        for field in ("price", "standard_error", "ci_95_low", "ci_95_high"):
            self.assertIn(field, result)

    def test_european_lr_greeks_returns_all_five_with_standard_errors(self):
        result = handle_request({
            "product": "european", "request": "lr_greeks", "spot": 100, "strike": 100,
            "rate": 0.05, "carry_yield": 0.0, "vol": 0.2, "time": 1.0, "type": "call",
            "path_count": 50_000, "seed": 1,
        })
        for field in ("delta", "delta_se", "gamma", "gamma_se", "vega", "vega_se",
                      "theta", "theta_se", "rho", "rho_se"):
            self.assertIn(field, result)

    def test_digital_lr_greeks_returns_all_five(self):
        result = handle_request({
            "product": "digital", "request": "lr_greeks", "spot": 100, "strike": 100,
            "rate": 0.05, "carry_yield": 0.0, "vol": 0.2, "time": 1.0, "type": "call",
            "digital_style": "cash_or_nothing", "path_count": 50_000, "seed": 1,
        })
        for field in ("delta", "gamma", "vega", "theta", "rho"):
            self.assertIn(field, result)

    def test_barrier_lr_greeks_omits_theta(self):
        result = handle_request({
            "product": "barrier", "request": "lr_greeks", "spot": 100, "strike": 100,
            "barrier": 120, "rate": 0.05, "carry_yield": 0.0, "vol": 0.2, "time": 1.0,
            "type": "call", "direction": "up", "knock": "out", "monitoring_points": 20,
            "path_count": 20_000, "seed": 1,
        })
        for field in ("delta", "gamma", "vega", "rho"):
            self.assertIn(field, result)
        self.assertNotIn("theta", result)

    def test_european_pathwise_greeks_returns_three_no_gamma_or_theta(self):
        result = handle_request({
            "product": "european", "request": "pathwise_greeks", "spot": 100, "strike": 100,
            "rate": 0.05, "carry_yield": 0.0, "vol": 0.2, "time": 1.0, "type": "call",
            "path_count": 50_000, "seed": 1,
        })
        for field in ("delta", "vega", "rho"):
            self.assertIn(field, result)
        for field in ("gamma", "theta"):
            self.assertNotIn(field, result)

    def test_asian_pathwise_greeks_returns_three(self):
        result = handle_request({
            "product": "asian", "request": "pathwise_greeks", "spot": 100, "strike": 100,
            "rate": 0.05, "carry_yield": 0.0, "vol": 0.2, "time": 1.0, "type": "call",
            "strike_style": "fixed", "average_style": "arithmetic", "monitoring_points": 12,
            "path_count": 20_000, "seed": 1,
        })
        for field in ("delta", "vega", "rho"):
            self.assertIn(field, result)

    def test_european_qmc_sobol_returns_price_without_standard_error(self):
        result = handle_request({
            "product": "european", "request": "qmc_sobol", "spot": 100, "strike": 100,
            "rate": 0.05, "carry_yield": 0.0, "vol": 0.2, "time": 1.0, "type": "call",
            "path_count": 20_000,
        })
        self.assertIn("price", result)
        self.assertIn("note", result)
        self.assertNotIn("standard_error", result)
        self.assertNotIn("seed", result)

    def test_asian_qmc_sobol_returns_price(self):
        result = handle_request({
            "product": "asian", "request": "qmc_sobol", "spot": 100, "strike": 100,
            "rate": 0.05, "carry_yield": 0.0, "vol": 0.25, "time": 1.0, "type": "call",
            "strike_style": "fixed", "average_style": "arithmetic", "monitoring_points": 7,
            "path_count": 10_000,
        })
        self.assertIn("price", result)
        self.assertIn("note", result)

    def test_asian_qmc_sobol_rejects_monitoring_points_above_cap(self):
        with self.assertRaises(RequestError):
            handle_request({
                "product": "asian", "request": "qmc_sobol", "spot": 100, "strike": 100,
                "rate": 0.05, "carry_yield": 0.0, "vol": 0.25, "time": 1.0, "type": "call",
                "strike_style": "fixed", "average_style": "arithmetic",
                "monitoring_points": 9, "path_count": 10_000,
            })

    def test_asian_qmc_sobol_rejects_geometric_average_style(self):
        with self.assertRaises(RequestError):
            handle_request({
                "product": "asian", "request": "qmc_sobol", "spot": 100, "strike": 100,
                "rate": 0.05, "carry_yield": 0.0, "vol": 0.25, "time": 1.0, "type": "call",
                "strike_style": "fixed", "average_style": "geometric",
                "monitoring_points": 5, "path_count": 10_000,
            })


if __name__ == "__main__":
    unittest.main()
