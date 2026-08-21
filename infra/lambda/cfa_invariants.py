"""CFA invariant checks for the demo's "live and green" table (docs/design/07-aws-demo.md
sec.4 item 6). Mirrors tools/generate_report.py's cfa_invariant_table() -- the same checks,
in a separate copy, since the Lambda image's Docker build context doesn't reach into
tools/. Kept in sync by hand; both ultimately check the same claims
tests/cfa_invariants_test.cpp makes in C++.
"""

import math

import mcd


def cfa_invariant_table():
    rows = []
    tol = 1e-9

    s, r, q, t = 100.0, 0.04, 0.015, 1.5
    expected = s * math.exp((r - q) * t)
    actual = mcd.forward_price(s, r, q, t)
    rows.append({"module": "LM4", "invariant": "Cost of carry",
                 "pass": abs(actual - expected) < tol, "deviation": abs(actual - expected)})

    f0 = mcd.forward_price(100.0, 0.03, 0.01, 1.0)
    v0 = mcd.forward_value(f0, f0, 0.03, 1.0)
    rows.append({"module": "LM5", "invariant": "Forward value at initiation is zero",
                 "pass": abs(v0) < tol, "deviation": abs(v0)})

    f0b, ft, rb, tt = 105.0, 112.0, 0.03, 0.4
    expected = (ft - f0b) * math.exp(-rb * tt)
    actual = mcd.forward_value(ft, f0b, rb, tt)
    rows.append({"module": "LM5", "invariant": "Forward value during life",
                 "pass": abs(actual - expected) < tol, "deviation": abs(actual - expected)})

    call = mcd.black_scholes_merton(100, 100, 0.04, 0.01, 0.25, 1.0, mcd.OptionType.Call)
    put = mcd.black_scholes_merton(100, 100, 0.04, 0.01, 0.25, 1.0, mcd.OptionType.Put)
    lhs = call + 100 * math.exp(-0.04 * 1.0)
    rhs = put + 100 * math.exp(-0.01 * 1.0)
    rows.append({"module": "LM9", "invariant": "Put-call parity",
                 "pass": abs(lhs - rhs) < 1e-12, "deviation": abs(lhs - rhs)})

    f0c = mcd.forward_price(100, 0.04, 0.01, 1.0)
    rhs2 = put + f0c * math.exp(-0.04 * 1.0)
    rows.append({"module": "LM9", "invariant": "Put-call forward parity",
                 "pass": abs(lhs - rhs2) < 1e-12, "deviation": abs(lhs - rhs2)})

    binom = mcd.crr_binomial(100, 100, 0.05, 0.01, 0.25, 1.0, 1, mcd.OptionType.Call)
    expected_pi = (math.exp((0.05 - 0.01) * 1.0) - binom.down_factor) / (
        binom.up_factor - binom.down_factor)
    ok = (abs(binom.risk_neutral_probability - expected_pi) < tol
          and 0 < binom.risk_neutral_probability < 1)
    rows.append({"module": "LM10", "invariant": "Binomial risk-neutrality", "pass": ok,
                 "deviation": abs(binom.risk_neutral_probability - expected_pi)})

    bsm = mcd.black_scholes_merton(100, 100, 0.05, 0.02, 0.2, 1.0, mcd.OptionType.Call)
    prev_error = float("inf")
    monotone = True
    for n in (10, 50, 250, 1000, 5000):
        price = mcd.crr_binomial(100, 100, 0.05, 0.02, 0.2, 1.0, n, mcd.OptionType.Call).price
        err = abs(price - bsm)
        if err >= prev_error:
            monotone = False
        prev_error = err
    rows.append({"module": "LM10",
                 "invariant": "Binomial converges to BSM (monotonically, error<0.01 at n=5000)",
                 "pass": monotone and prev_error < 0.01, "deviation": prev_error})

    bounds_ok = True
    for s2 in (50.0, 100.0, 150.0):
        for k2 in (80.0, 100.0, 120.0):
            call2 = mcd.black_scholes_merton(s2, k2, 0.04, 0.015, 0.3, 1.0, mcd.OptionType.Call)
            lower = max(s2 * math.exp(-0.015) - k2 * math.exp(-0.04), 0.0)
            upper = s2 * math.exp(-0.015)
            if not (lower - tol <= call2 <= upper + tol):
                bounds_ok = False
    rows.append({"module": "LM4/LM8", "invariant": "No-arbitrage bounds", "pass": bounds_ok,
                 "deviation": 0.0})

    mono_ok = True
    prev = -1.0
    for s3 in [50.0 + 5.0 * i for i in range(21)]:
        price = mcd.black_scholes_merton(s3, 100, 0.04, 0.01, 0.25, 1.0, mcd.OptionType.Call)
        if price < prev - tol:
            mono_ok = False
        prev = price
    rows.append({"module": "LM8", "invariant": "Monotonicity in spot", "pass": mono_ok,
                 "deviation": 0.0})

    return rows
