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


if __name__ == "__main__":
    unittest.main()
