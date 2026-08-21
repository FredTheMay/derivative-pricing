"""Request validation and dispatch for the AWS demo's Lambda handler.

Reimplements the *same request/response schema* apps/mcd_cli/main.cpp defines (same field
names, same per-product required fields, same seven-field Monte Carlo result shape) as its
own Python module -- necessarily separate code, since a C++ binary and a Python Lambda
can't share a source file, but the same protocol. See
docs/design/07-aws-demo.md sec.3.2.

Adds one guardrail mcd_cli doesn't need: a hard cap on path_count (CLAUDE.md sec.6 Phase 7
cost guardrail -- a public, unauthenticated endpoint must not be able to trigger an
arbitrarily expensive computation).
"""

import time
from types import SimpleNamespace

import mcd

MAX_PATH_COUNT = 5_000_000
MAX_EXACT_INTEGER = 9007199254740992  # 2^53, same bound mcd_cli enforces on seed/path_count

Z_975 = 1.959963985  # two-sided 95% normal critical value


class RequestError(Exception):
    """A malformed or out-of-bounds request. Always caught by the handler and turned into a
    clean 400 response -- never allowed to surface as an unhandled Lambda error."""


def _require(req, key):
    if key not in req:
        raise RequestError(f"missing required field '{key}'")
    return req[key]


def _require_number(req, key):
    value = _require(req, key)
    if not isinstance(value, (int, float)) or isinstance(value, bool):
        raise RequestError(f"field '{key}' must be a number")
    return float(value)


def _optional_number(req, key, default):
    return float(req[key]) if key in req else default


def _require_string(req, key):
    value = _require(req, key)
    if not isinstance(value, str):
        raise RequestError(f"field '{key}' must be a string")
    return value


def _optional_string(req, key, default):
    return req.get(key, default)


def _require_count(req, key):
    d = _require_number(req, key)
    if d < 0 or d != int(d) or d > MAX_EXACT_INTEGER:
        raise RequestError(
            f"field '{key}' must be a non-negative integer no larger than 2^53")
    return int(d)


def _require_path_count(req):
    n = _require_count(req, "path_count")
    if n > MAX_PATH_COUNT:
        raise RequestError(
            f"'path_count' must not exceed {MAX_PATH_COUNT:,} on this demo endpoint "
            "(CLAUDE.md sec.6 Phase 7 cost guardrail) -- run the full engine locally "
            "for larger studies")
    return n


def _require_int(req, key):
    d = _require_number(req, key)
    if d != int(d):
        raise RequestError(f"field '{key}' must be an integer")
    return int(d)


def _option_type(req, key="type"):
    s = _require_string(req, key)
    if s == "call":
        return mcd.OptionType.Call
    if s == "put":
        return mcd.OptionType.Put
    raise RequestError("'type' must be 'call' or 'put'")


def _direction(req):
    s = _require_string(req, "direction")
    if s == "up":
        return mcd.BarrierDirection.Up
    if s == "down":
        return mcd.BarrierDirection.Down
    raise RequestError("'direction' must be 'up' or 'down'")


def _knock(req):
    s = _require_string(req, "knock")
    if s == "in":
        return mcd.BarrierKnock.In
    if s == "out":
        return mcd.BarrierKnock.Out
    raise RequestError("'knock' must be 'in' or 'out'")


def _strike_style(req, key="strike_style"):
    s = _require_string(req, key)
    if s == "fixed":
        return mcd.StrikeStyle.Fixed
    if s == "floating":
        return mcd.StrikeStyle.Floating
    raise RequestError(f"'{key}' must be 'fixed' or 'floating'")


def _average_style(req):
    s = _require_string(req, "average_style")
    if s == "arithmetic":
        return mcd.AverageStyle.Arithmetic
    if s == "geometric":
        return mcd.AverageStyle.Geometric
    raise RequestError("'average_style' must be 'arithmetic' or 'geometric'")


def _digital_style(req):
    s = _require_string(req, "digital_style")
    if s == "cash_or_nothing":
        return mcd.DigitalStyle.CashOrNothing
    if s == "asset_or_nothing":
        return mcd.DigitalStyle.AssetOrNothing
    raise RequestError("'digital_style' must be 'cash_or_nothing' or 'asset_or_nothing'")


def _mc_result_to_dict(result, seed, elapsed):
    """Every Monte Carlo product's response carries all seven fields below -- this is the
    only place an McResult becomes a response dict, so a price without its confidence
    interval is unrepresentable, not just avoided by convention."""
    pps = result.path_count / elapsed if elapsed > 0 else 0.0
    return {
        "price": result.price,
        "standard_error": result.standard_error,
        "ci_95_low": result.price - Z_975 * result.standard_error,
        "ci_95_high": result.price + Z_975 * result.standard_error,
        "path_count": result.path_count,
        "seed": seed,
        "elapsed_seconds": elapsed,
        "paths_per_second": pps,
    }


def _analytic_result_to_dict(price, elapsed):
    return {"price": price, "elapsed_seconds": elapsed}


def _timed(fn):
    """Mirrors mcd::time_call (Phase 6): wall-clock the pricing call itself, not JSON
    parsing/validation around it."""
    start = time.perf_counter()
    result = fn()
    return result, time.perf_counter() - start


def _mc_options(req):
    """Optional variance-reduction toggles, for the demo's antithetic/control-variate
    on-vs-off comparison (docs/design/07-aws-demo.md sec.4 item 3) -- not part of
    mcd_cli's schema (Phase 6 didn't need it), added here since this endpoint does."""
    options = mcd.McOptions()
    options.antithetic = bool(req.get("antithetic", False))
    options.control_variate = bool(req.get("control_variate", False))
    return options


def handle_request(req):
    if not isinstance(req, dict):
        raise RequestError("request body must be a JSON object")

    product = _require_string(req, "product")
    request_kind = _optional_string(req, "request", "price")

    if product == "forward":
        spot = _require_number(req, "spot")
        rate = _require_number(req, "rate")
        carry_yield = _require_number(req, "carry_yield")
        expiry = _require_number(req, "time")
        price, elapsed = _timed(lambda: mcd.forward_price(spot, rate, carry_yield, expiry))
        return _analytic_result_to_dict(price, elapsed)

    if product in ("binomial_european", "binomial_american"):
        spot = _require_number(req, "spot")
        strike = _require_number(req, "strike")
        rate = _require_number(req, "rate")
        carry_yield = _require_number(req, "carry_yield")
        vol = _require_number(req, "vol")
        expiry = _require_number(req, "time")
        option_type = _option_type(req)
        steps = _require_int(req, "steps")
        if steps < 1 or steps > 20_000:
            raise RequestError("'steps' must be between 1 and 20,000 on this demo endpoint")

        if product == "binomial_american":
            price, elapsed = _timed(lambda: mcd.crr_binomial_american(
                spot, strike, rate, carry_yield, vol, expiry, steps, option_type))
            return _analytic_result_to_dict(price, elapsed)

        result, elapsed = _timed(lambda: mcd.crr_binomial(
            spot, strike, rate, carry_yield, vol, expiry, steps, option_type))
        out = _analytic_result_to_dict(result.price, elapsed)
        out["risk_neutral_probability"] = result.risk_neutral_probability
        out["up_factor"] = result.up_factor
        out["down_factor"] = result.down_factor
        return out

    if product == "european":
        spot = _require_number(req, "spot")
        strike = _require_number(req, "strike")
        rate = _require_number(req, "rate")
        carry_yield = _require_number(req, "carry_yield")
        vol = _require_number(req, "vol")
        expiry = _require_number(req, "time")
        option_type = _option_type(req)

        if request_kind == "greeks":
            path_count = _require_path_count(req)
            seed = _require_count(req, "seed")
            bumps = mcd.default_bump_sizes(spot, vol, expiry)
            g, elapsed = _timed(lambda: mcd.finite_difference_european(
                spot, strike, rate, carry_yield, vol, expiry, option_type, path_count, seed,
                bumps))
            return {"delta": g.delta, "gamma": g.gamma, "vega": g.vega, "theta": g.theta,
                    "rho": g.rho, "elapsed_seconds": elapsed}

        path_count = _require_path_count(req)
        seed = _require_count(req, "seed")
        options = _mc_options(req)
        result, elapsed = _timed(lambda: mcd.monte_carlo_european(
            spot, strike, rate, carry_yield, vol, expiry, option_type, path_count, seed,
            options))
        return _mc_result_to_dict(result, seed, elapsed)

    if product == "digital":
        spot = _require_number(req, "spot")
        strike = _require_number(req, "strike")
        rate = _require_number(req, "rate")
        carry_yield = _require_number(req, "carry_yield")
        vol = _require_number(req, "vol")
        expiry = _require_number(req, "time")
        option_type = _option_type(req)
        style = _digital_style(req)
        cash_amount = _optional_number(req, "cash_amount", 1.0)
        path_count = _require_path_count(req)
        seed = _require_count(req, "seed")
        result, elapsed = _timed(lambda: mcd.monte_carlo_digital(
            spot, strike, rate, carry_yield, vol, expiry, option_type, style, cash_amount,
            path_count, seed))
        return _mc_result_to_dict(result, seed, elapsed)

    if product == "asian":
        spot = _require_number(req, "spot")
        strike = _require_number(req, "strike")
        rate = _require_number(req, "rate")
        carry_yield = _require_number(req, "carry_yield")
        vol = _require_number(req, "vol")
        expiry = _require_number(req, "time")
        option_type = _option_type(req)
        strike_style = _strike_style(req, "strike_style")
        average_style = _average_style(req)
        monitoring_points = _require_int(req, "monitoring_points")
        path_count = _require_path_count(req)
        seed = _require_count(req, "seed")
        options = _mc_options(req)
        result, elapsed = _timed(lambda: mcd.monte_carlo_asian(
            spot, strike, rate, carry_yield, vol, expiry, option_type, strike_style,
            average_style, monitoring_points, path_count, seed, options))
        return _mc_result_to_dict(result, seed, elapsed)

    if product == "barrier":
        spot = _require_number(req, "spot")
        strike = _require_number(req, "strike")
        barrier = _require_number(req, "barrier")
        rate = _require_number(req, "rate")
        carry_yield = _require_number(req, "carry_yield")
        vol = _require_number(req, "vol")
        expiry = _require_number(req, "time")
        option_type = _option_type(req)
        direction = _direction(req)
        knock = _knock(req)
        rebate = _optional_number(req, "rebate", 0.0)
        monitoring_points = _require_int(req, "monitoring_points")
        path_count = _require_path_count(req)
        seed = _require_count(req, "seed")
        result, elapsed = _timed(lambda: mcd.monte_carlo_barrier(
            spot, strike, barrier, rate, carry_yield, vol, expiry, option_type, direction,
            knock, rebate, monitoring_points, path_count, seed))
        return _mc_result_to_dict(result, seed, elapsed)

    if product == "lookback":
        spot = _require_number(req, "spot")
        strike = _optional_number(req, "strike", 0.0)
        rate = _require_number(req, "rate")
        carry_yield = _require_number(req, "carry_yield")
        vol = _require_number(req, "vol")
        expiry = _require_number(req, "time")
        option_type = _option_type(req)
        style = _strike_style(req, "style")
        monitoring_points = _require_int(req, "monitoring_points")
        path_count = _require_path_count(req)
        seed = _require_count(req, "seed")
        result, elapsed = _timed(lambda: mcd.monte_carlo_lookback(
            spot, strike, rate, carry_yield, vol, expiry, option_type, style,
            monitoring_points, path_count, seed))
        return _mc_result_to_dict(result, seed, elapsed)

    if product == "american":
        spot = _require_number(req, "spot")
        strike = _require_number(req, "strike")
        rate = _require_number(req, "rate")
        carry_yield = _require_number(req, "carry_yield")
        vol = _require_number(req, "vol")
        expiry = _require_number(req, "time")
        option_type = _option_type(req)
        monitoring_points = _require_int(req, "monitoring_points")
        path_count = _require_path_count(req)
        seed = _require_count(req, "seed")
        result, elapsed = _timed(lambda: mcd.monte_carlo_lsm_american(
            spot, strike, rate, carry_yield, vol, expiry, option_type, monitoring_points,
            path_count, seed))
        # LsmResult doesn't carry path_count (Phase 5); _mc_result_to_dict needs it, so
        # wrap in a plain namespace with the same three fields rather than adding a field
        # to the C++ struct just for this.
        as_mc = SimpleNamespace(price=result.price, standard_error=result.standard_error,
                                 path_count=path_count)
        return _mc_result_to_dict(as_mc, seed, elapsed)

    raise RequestError(f"unknown 'product': '{product}'")
