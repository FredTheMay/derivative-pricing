#!/usr/bin/env python3
"""Regenerates the data-driven sections of docs/validation-report.md from a live run of
the engine (via the `mcd` Python bindings), per CLAUDE.md sec.6 Phase 6 and
docs/design/06-cli-bindings-reporting.md sec.5. Writes only the block delimited by
`<!-- BEGIN GENERATED -->` / `<!-- END GENERATED -->` -- everything else in the document
(five phases of hand-written narrative) is left untouched.

Standard library only, plus the `mcd` module itself (already-approved dependencies, per
docs/design/06-cli-bindings-reporting.md sec.2.1/sec.4) -- no numpy, no matplotlib. SVG
charts are hand-written strings with computed coordinates, the same approach used for
every chart in this project since Phase 3.

Usage: python tools/generate_report.py
"""

import datetime
import math
import os
import time
from pathlib import Path

import mcd

REPO_ROOT = Path(__file__).resolve().parent.parent
VALIDATION_REPORT = REPO_ROOT / "docs" / "validation-report.md"
BENCHMARKS_DIR = REPO_ROOT / "docs" / "benchmarks"
BEGIN_MARKER = "<!-- BEGIN GENERATED -->"
END_MARKER = "<!-- END GENERATED -->"


# --------------------------------------------------------------------------------------
# Small hand-written SVG line-chart helper (log-log axes), shared by every chart below --
# no plotting library, per CLAUDE.md sec.5.
# --------------------------------------------------------------------------------------


def _svg_log_log_chart(title, series, x_label, y_label, path):
    """series: list of (name, color, [(x, y), ...]) with x, y > 0."""
    all_x = [math.log10(x) for _, _, pts in series for x, _ in pts]
    all_y = [math.log10(y) for _, _, pts in series for _, y in pts]
    x_min, x_max = min(all_x), max(all_x)
    y_min, y_max = math.floor(min(all_y)) - 0.2, math.ceil(max(all_y)) + 0.2
    left, right, top, bottom = 90, 660, 40, 430

    def px(x):
        return left + (x - x_min) / (x_max - x_min) * (right - left)

    def py(y):
        return bottom - (y - y_min) / (y_max - y_min) * (bottom - top)

    x_ticks = list(range(math.ceil(x_min), math.floor(x_max) + 1))
    y_ticks = list(range(math.ceil(y_min), math.floor(y_max) + 1))

    lines = [
        '<svg xmlns="http://www.w3.org/2000/svg" width="720" height="500" '
        'viewBox="0 0 720 500" font-family="Helvetica, Arial, sans-serif">',
        '<rect width="720" height="500" fill="#ffffff"/>',
        f'<text x="360" y="22" text-anchor="middle" font-size="16" font-weight="bold" '
        f'fill="#1a1a1a">{title}</text>',
        '<g stroke="#e0e0e0" stroke-width="1">',
    ]
    for yt in y_ticks:
        lines.append(f'<line x1="{left}" y1="{py(yt):.1f}" x2="{right}" y2="{py(yt):.1f}"/>')
    lines.append("</g>")
    lines.append('<g font-size="12" fill="#555555" text-anchor="end">')
    for yt in y_ticks:
        lines.append(f'<text x="{left - 6}" y="{py(yt) + 4:.1f}">10^{yt}</text>')
    lines.append("</g>")
    lines.append('<g stroke="#e0e0e0" stroke-width="1">')
    for xt in x_ticks:
        lines.append(f'<line x1="{px(xt):.1f}" y1="{top}" x2="{px(xt):.1f}" y2="{bottom}"/>')
    lines.append("</g>")
    lines.append('<g font-size="12" fill="#555555" text-anchor="middle">')
    for xt in x_ticks:
        lines.append(f'<text x="{px(xt):.1f}" y="{bottom + 18}">10^{xt}</text>')
    lines.append("</g>")
    lines.append(f'<line x1="{left}" y1="{top}" x2="{left}" y2="{bottom}" '
                  f'stroke="#1a1a1a" stroke-width="1.5"/>')
    lines.append(f'<line x1="{left}" y1="{bottom}" x2="{right}" y2="{bottom}" '
                  f'stroke="#1a1a1a" stroke-width="1.5"/>')
    lines.append(f'<text x="{(left + right) / 2:.1f}" y="478" text-anchor="middle" '
                  f'font-size="13" fill="#1a1a1a">{x_label}</text>')
    mid_y = (top + bottom) / 2
    lines.append(f'<text x="18" y="{mid_y:.1f}" text-anchor="middle" font-size="13" '
                  f'fill="#1a1a1a" transform="rotate(-90 18 {mid_y:.1f})">{y_label}</text>')

    legend_x = left + 10
    for name, color, pts in series:
        log_pts = [(math.log10(x), math.log10(y)) for x, y in pts]
        path_d = "M" + " L".join(f"{px(x):.1f},{py(y):.1f}" for x, y in log_pts)
        lines.append(f'<path d="{path_d}" fill="none" stroke="{color}" stroke-width="2"/>')
        for x, y in log_pts:
            lines.append(f'<circle cx="{px(x):.1f}" cy="{py(y):.1f}" r="3" fill="{color}"/>')
        lines.append(f'<line x1="{legend_x}" y1="{top + 14}" x2="{legend_x + 24}" y2="{top + 14}" '
                      f'stroke="{color}" stroke-width="2"/>')
        lines.append(f'<text x="{legend_x + 30}" y="{top + 18}" font-size="12" '
                      f'fill="#1a1a1a">{name}</text>')
        legend_x += 160

    lines.append("</svg>")
    path.write_text("\n".join(lines) + "\n")


def _linear_regression_slope(xs, ys):
    n = len(xs)
    mean_x = sum(xs) / n
    mean_y = sum(ys) / n
    num = sum((x - mean_x) * (y - mean_y) for x, y in zip(xs, ys))
    den = sum((x - mean_x) ** 2 for x in xs)
    return num / den


# --------------------------------------------------------------------------------------
# 1. CFA invariant table -- recomputes tests/cfa_invariants_test.cpp's checks live.
# --------------------------------------------------------------------------------------


def cfa_invariant_table():
    rows = []
    tol = 1e-9

    s, r, q, t = 100.0, 0.04, 0.015, 1.5
    expected = s * math.exp((r - q) * t)
    actual = mcd.forward_price(s, r, q, t)
    rows.append(("LM4", "Cost of carry", abs(actual - expected) < tol, abs(actual - expected)))

    f0 = mcd.forward_price(100.0, 0.03, 0.01, 1.0)
    v0 = mcd.forward_value(f0, f0, 0.03, 1.0)
    rows.append(("LM5", "Forward value at initiation is zero", abs(v0) < tol, abs(v0)))

    f0b, ft, rb, tt = 105.0, 112.0, 0.03, 0.4
    expected = (ft - f0b) * math.exp(-rb * tt)
    actual = mcd.forward_value(ft, f0b, rb, tt)
    rows.append(("LM5", "Forward value during life", abs(actual - expected) < tol,
                 abs(actual - expected)))

    call = mcd.black_scholes_merton(100, 100, 0.04, 0.01, 0.25, 1.0, mcd.OptionType.Call)
    put = mcd.black_scholes_merton(100, 100, 0.04, 0.01, 0.25, 1.0, mcd.OptionType.Put)
    lhs = call + 100 * math.exp(-0.04 * 1.0)
    rhs = put + 100 * math.exp(-0.01 * 1.0)
    rows.append(("LM9", "Put-call parity", abs(lhs - rhs) < 1e-12, abs(lhs - rhs)))

    f0c = mcd.forward_price(100, 0.04, 0.01, 1.0)
    rhs2 = put + f0c * math.exp(-0.04 * 1.0)
    rows.append(("LM9", "Put-call forward parity", abs(lhs - rhs2) < 1e-12, abs(lhs - rhs2)))

    binom = mcd.crr_binomial(100, 100, 0.05, 0.01, 0.25, 1.0, 1, mcd.OptionType.Call)
    expected_pi = (math.exp((0.05 - 0.01) * 1.0) - binom.down_factor) / (
        binom.up_factor - binom.down_factor)
    ok = abs(binom.risk_neutral_probability - expected_pi) < tol and 0 < binom.risk_neutral_probability < 1
    rows.append(("LM10", "Binomial risk-neutrality",
                 ok, abs(binom.risk_neutral_probability - expected_pi)))

    bsm = mcd.black_scholes_merton(100, 100, 0.05, 0.02, 0.2, 1.0, mcd.OptionType.Call)
    prev_error = float("inf")
    monotone = True
    for n in (10, 50, 250, 1000, 5000):
        price = mcd.crr_binomial(100, 100, 0.05, 0.02, 0.2, 1.0, n, mcd.OptionType.Call).price
        err = abs(price - bsm)
        if err >= prev_error:
            monotone = False
        prev_error = err
    rows.append(("LM10", "Binomial converges to BSM (monotonically, error<0.01 at n=5000)",
                 monotone and prev_error < 0.01, prev_error))

    bounds_ok = True
    for s2 in (50.0, 100.0, 150.0):
        for k2 in (80.0, 100.0, 120.0):
            call2 = mcd.black_scholes_merton(s2, k2, 0.04, 0.015, 0.3, 1.0, mcd.OptionType.Call)
            lower = max(s2 * math.exp(-0.015) - k2 * math.exp(-0.04), 0.0)
            upper = s2 * math.exp(-0.015)
            if not (lower - tol <= call2 <= upper + tol):
                bounds_ok = False
    rows.append(("LM4/LM8", "No-arbitrage bounds", bounds_ok, 0.0))

    mono_ok = True
    prev = -1.0
    for s3 in [50.0 + 5.0 * i for i in range(21)]:
        price = mcd.black_scholes_merton(s3, 100, 0.04, 0.01, 0.25, 1.0, mcd.OptionType.Call)
        if price < prev - tol:
            mono_ok = False
        prev = price
    rows.append(("LM8", "Monotonicity in spot", mono_ok, 0.0))

    return rows


# --------------------------------------------------------------------------------------
# 2. Convergence sweep: log-log SE vs. path count for European.
# --------------------------------------------------------------------------------------


def convergence_sweep():
    path_counts = [1_000, 10_000, 100_000, 1_000_000]
    points = []
    for n in path_counts:
        result = mcd.monte_carlo_european(100, 100, 0.05, 0.0, 0.2, 1.0, mcd.OptionType.Call,
                                            n, 123)
        points.append((n, result.standard_error))
    xs = [math.log10(x) for x, _ in points]
    ys = [math.log10(y) for _, y in points]
    slope = _linear_regression_slope(xs, ys)
    _svg_log_log_chart(
        "Monte Carlo European Call: Standard Error vs. Path Count (log-log, live-generated)",
        [("standard error", "#2166ac", points)],
        "path count", "standard error",
        BENCHMARKS_DIR / "generated-convergence.svg",
    )
    return points, slope


# --------------------------------------------------------------------------------------
# 3. Variance-reduction factor table.
# --------------------------------------------------------------------------------------


def variance_reduction_table():
    n, seed = 200_000, 7
    rows = []

    plain = mcd.monte_carlo_european(100, 100, 0.05, 0.0, 0.2, 1.0, mcd.OptionType.Call, n, seed)
    anti_options = mcd.McOptions()
    anti_options.antithetic = True
    anti = mcd.monte_carlo_european(100, 100, 0.05, 0.0, 0.2, 1.0, mcd.OptionType.Call, n, seed,
                                      anti_options)
    factor = (plain.standard_error ** 2) / (anti.standard_error ** 2)
    rows.append(("European", "antithetic", factor))

    plain_asian = mcd.monte_carlo_asian(100, 100, 0.05, 0.0, 0.2, 1.0, mcd.OptionType.Call,
                                          mcd.StrikeStyle.Fixed, mcd.AverageStyle.Arithmetic, 12,
                                          n, seed)
    cv_options = mcd.McOptions()
    cv_options.control_variate = True
    cv_asian = mcd.monte_carlo_asian(100, 100, 0.05, 0.0, 0.2, 1.0, mcd.OptionType.Call,
                                       mcd.StrikeStyle.Fixed, mcd.AverageStyle.Arithmetic, 12, n,
                                       seed, cv_options)
    factor_cv = (plain_asian.standard_error ** 2) / (cv_asian.standard_error ** 2)
    rows.append(("Arithmetic Asian", "control variate (geometric Asian)", factor_cv))

    return rows


# --------------------------------------------------------------------------------------
# 4. Bump-size sweep for FD Greeks (delta, gamma vs. h/S).
# --------------------------------------------------------------------------------------


def bump_size_sweep():
    s, k, r, q, sigma, t = 100.0, 100.0, 0.05, 0.0, 0.20, 1.0
    n, seed = 200_000, 777

    def analytic_delta():
        h = s * 1e-6
        return (mcd.black_scholes_merton(s + h, k, r, q, sigma, t, mcd.OptionType.Call) -
                mcd.black_scholes_merton(s - h, k, r, q, sigma, t, mcd.OptionType.Call)) / (2 * h)

    def analytic_gamma():
        h = s * 1e-4
        v0 = mcd.black_scholes_merton(s, k, r, q, sigma, t, mcd.OptionType.Call)
        return (mcd.black_scholes_merton(s + h, k, r, q, sigma, t, mcd.OptionType.Call) -
                2 * v0 +
                mcd.black_scholes_merton(s - h, k, r, q, sigma, t, mcd.OptionType.Call)) / (h * h)

    true_delta, true_gamma = analytic_delta(), analytic_gamma()
    delta_points, gamma_points = [], []
    frac = 1e-6
    while frac <= 1.0 + 1e-9:
        h = s * frac
        v_plus = mcd.monte_carlo_european(s + h, k, r, q, sigma, t, mcd.OptionType.Call, n, seed)
        v_minus = mcd.monte_carlo_european(s - h, k, r, q, sigma, t, mcd.OptionType.Call, n, seed)
        v0 = mcd.monte_carlo_european(s, k, r, q, sigma, t, mcd.OptionType.Call, n, seed)
        delta_fd = (v_plus.price - v_minus.price) / (2 * h)
        gamma_fd = (v_plus.price - 2 * v0.price + v_minus.price) / (h * h)
        delta_err = abs(delta_fd - true_delta)
        gamma_err = abs(gamma_fd - true_gamma)
        if delta_err > 0 and gamma_err > 0:
            delta_points.append((frac, delta_err))
            gamma_points.append((frac, gamma_err))
        frac *= 3.16227766017

    _svg_log_log_chart(
        "Finite-Difference Greeks: Error vs. Bump Size h/S (log-log, live-generated)",
        [("delta error", "#2166ac", delta_points), ("gamma error", "#b2182b", gamma_points)],
        "bump fraction h/S", "absolute error vs. analytic BSM",
        BENCHMARKS_DIR / "generated-bump-size-sweep.svg",
    )
    best_gamma = min(gamma_points, key=lambda p: p[1])
    return best_gamma


# --------------------------------------------------------------------------------------
# 5. Thread-scaling sweep.
# --------------------------------------------------------------------------------------


def scaling_sweep():
    hw_concurrency = os.cpu_count() or 1
    thread_counts = sorted(set([1, 2, max(1, hw_concurrency // 2), hw_concurrency]))
    path_count = 2_000_000
    points = []
    base_time = None
    for threads in thread_counts:
        options = mcd.McOptions()
        options.num_threads = threads
        start = time.perf_counter()
        mcd.monte_carlo_european(100, 100, 0.05, 0.0, 0.2, 1.0, mcd.OptionType.Call, path_count,
                                   42, options)
        elapsed = time.perf_counter() - start
        if base_time is None:
            base_time = elapsed
        speedup = base_time / elapsed
        points.append((threads, speedup))

    _svg_log_log_chart(
        f"Thread Scaling: Speedup vs. Thread Count (log-log, live-generated, "
        f"hardware_concurrency={hw_concurrency})",
        [("speedup", "#2166ac", points)],
        "thread count", "speedup vs. 1 thread",
        BENCHMARKS_DIR / "generated-scaling.svg",
    )
    max_threads, max_speedup = points[-1]
    efficiency = max_speedup / max_threads
    return points, efficiency, hw_concurrency


# --------------------------------------------------------------------------------------
# Assembly
# --------------------------------------------------------------------------------------


def build_markdown():
    now = datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%d %H:%M UTC")
    parts = [BEGIN_MARKER, "", f"_Live-regenerated by `python tools/generate_report.py` on "
             f"{now}. This block is mechanically overwritten on every run -- do not hand-edit "
             f"it; edit the narrative sections elsewhere in this document instead._", ""]

    parts.append("### CFA invariant results (live)")
    parts.append("")
    parts.append("| Module | Invariant | Result | Measured deviation |")
    parts.append("|---|---|---|---|")
    for module, name, ok, deviation in cfa_invariant_table():
        status = "PASS" if ok else "**FAIL**"
        parts.append(f"| {module} | {name} | {status} | {deviation:.3e} |")
    parts.append("")

    points, slope = convergence_sweep()
    parts.append("### Convergence: standard error vs. path count (live)")
    parts.append("")
    parts.append("| Path count | Standard error |")
    parts.append("|---:|---:|")
    for n, se in points:
        parts.append(f"| {n:,} | {se:.6f} |")
    parts.append("")
    parts.append(f"Fitted log-log slope: **{slope:.4f}** (theory: -0.5).")
    parts.append("")
    parts.append("![convergence](benchmarks/generated-convergence.svg)")
    parts.append("")

    parts.append("### Variance-reduction factors (live)")
    parts.append("")
    parts.append("| Product | Technique | Variance-reduction factor |")
    parts.append("|---|---|---:|")
    for product, technique, factor in variance_reduction_table():
        parts.append(f"| {product} | {technique} | {factor:.2f}x |")
    parts.append("")

    best_frac, best_err = bump_size_sweep()
    parts.append("### Bump-size sweep for finite-difference Greeks (live)")
    parts.append("")
    parts.append(f"Measured gamma-error minimum this run: h/S = {best_frac:.3e}, "
                 f"error = {best_err:.3e}.")
    parts.append("")
    parts.append("![bump-size](benchmarks/generated-bump-size-sweep.svg)")
    parts.append("")

    scaling_points, efficiency, hw = scaling_sweep()
    parts.append("### Thread scaling (live)")
    parts.append("")
    parts.append(f"`hardware_concurrency()` on this machine: {hw}.")
    parts.append("")
    parts.append("| Threads | Speedup vs. 1 thread |")
    parts.append("|---:|---:|")
    for threads, speedup in scaling_points:
        parts.append(f"| {threads} | {speedup:.2f}x |")
    parts.append("")
    parts.append(f"Parallel efficiency at max thread count: **{efficiency * 100:.1f}%**.")
    parts.append("")
    parts.append("![scaling](benchmarks/generated-scaling.svg)")
    parts.append("")

    parts.append(END_MARKER)
    return "\n".join(parts)


def main():
    generated = build_markdown()
    text = VALIDATION_REPORT.read_text()
    if BEGIN_MARKER not in text or END_MARKER not in text:
        raise SystemExit(
            f"{VALIDATION_REPORT} is missing {BEGIN_MARKER}/{END_MARKER} markers -- "
            "add them once (see docs/design/06-cli-bindings-reporting.md sec.2.2) before "
            "running this generator.")
    before = text[: text.index(BEGIN_MARKER)]
    after = text[text.index(END_MARKER) + len(END_MARKER):]
    VALIDATION_REPORT.write_text(before + generated + after)
    print(f"Regenerated {VALIDATION_REPORT}")


if __name__ == "__main__":
    main()
