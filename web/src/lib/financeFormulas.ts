// Pure, client-side implementations of every formula in the CFA Level I Derivatives
// educational dashboard (the "Learn" tabs in App.tsx). Deliberately independent of the
// C++ engine's own analytic pricers (include/mcd/pricers/analytic.hpp), which are
// continuous-compounding-only per CLAUDE.md's locked GBM model -- these formulas follow
// exactly the compounding convention specified for each (some discrete, some continuous),
// so the LaTeX rendered on screen always matches the number computed here, with no risk
// of a silent mismatch against a differently-conventioned backend function. None of this
// needs simulation: every formula below is deterministic time-value-of-money or terminal-
// payoff arithmetic, so it runs synchronously on every slider tick with no network call.

// ---------------------------------------------------------------------------
// Section 1 -- Market structure and cost of carry
// ---------------------------------------------------------------------------

/** FV_N = PV(1+r)^N -- discrete compounding future value. */
export function futureValueDiscrete(pv: number, r: number, n: number): number {
  return pv * Math.pow(1 + r, n);
}
export const LATEX_FV_DISCRETE = "FV_{N} = PV(1+r)^{N}";

/** FV_T = PV*e^{rT} -- continuous compounding future value. */
export function futureValueContinuous(pv: number, r: number, t: number): number {
  return pv * Math.exp(r * t);
}
export const LATEX_FV_CONTINUOUS = "FV_{T} = PV\\,e^{rT}";

/** F0(T) = S0(1+r)^T -- forward price, no additional cash flows, discrete compounding. */
export function forwardPriceDiscreteNoCashFlows(s0: number, r: number, t: number): number {
  return s0 * Math.pow(1 + r, t);
}
export const LATEX_FORWARD_DISCRETE_NO_CF = "F_{0}(T) = S_{0}(1+r)^{T}";

/**
 * F0(T) = [S0 - PV0(I) + PV0(C)](1+r)^T -- forward price with known discrete costs C
 * and benefits I, each expressed as a present value at t=0.
 */
export function forwardPriceDiscreteWithCashFlows(
  s0: number, pv0OfIncome: number, pv0OfCost: number, r: number, t: number,
): number {
  return (s0 - pv0OfIncome + pv0OfCost) * Math.pow(1 + r, t);
}
export const LATEX_FORWARD_DISCRETE_WITH_CF =
  "F_{0}(T) = [S_{0} - PV_{0}(I) + PV_{0}(C)](1+r)^{T}";

/** F0(T) = S0*e^{(r+c-i)T} -- forward price, continuous carry rates: income i, cost c. */
export function forwardPriceContinuousCarry(
  s0: number, r: number, c: number, i: number, t: number,
): number {
  return s0 * Math.exp((r + c - i) * t);
}
export const LATEX_FORWARD_CONTINUOUS_CARRY = "F_{0}(T) = S_{0}e^{(r+c-i)T}";

// ---------------------------------------------------------------------------
// Section 2 -- Forward/futures MTM valuation prior to expiration
// ---------------------------------------------------------------------------

/** PVt of F0(T) = F0(T)(1+r)^{-(T-t)} -- present value, at t, of the forward price fixed at 0. */
export function presentValueOfForwardPrice(
  f0: number, r: number, t: number, valuationTime: number,
): number {
  return f0 * Math.pow(1 + r, -(t - valuationTime));
}
export const LATEX_PV_OF_FORWARD = "PV_{t}\\text{ of }F_{0}(T) = F_{0}(T)(1+r)^{-(T-t)}";

/** Vt(T) = St - F0(T)(1+r)^{-(T-t)} -- long forward MTM value, no cash flows. */
export function forwardMtmNoCashFlows(
  st: number, f0: number, r: number, t: number, valuationTime: number,
): number {
  return st - presentValueOfForwardPrice(f0, r, t, valuationTime);
}
export const LATEX_FORWARD_MTM_NO_CF =
  "V_{t}(T) = S_{t} - F_{0}(T)(1+r)^{-(T-t)}";

/** Vt(T) = (St - PVt(I) + PVt(C)) - F0(T)(1+r)^{-(T-t)} -- long forward MTM, with cash flows. */
export function forwardMtmWithCashFlows(
  st: number, pvtOfIncome: number, pvtOfCost: number,
  f0: number, r: number, t: number, valuationTime: number,
): number {
  return (st - pvtOfIncome + pvtOfCost) - presentValueOfForwardPrice(f0, r, t, valuationTime);
}
export const LATEX_FORWARD_MTM_WITH_CF =
  "V_{t}(T) = (S_{t} - PV_{t}(I) + PV_{t}(C)) - F_{0}(T)(1+r)^{-(T-t)}";

// ---------------------------------------------------------------------------
// Section 3 -- FX forwards
// ---------------------------------------------------------------------------

/** F0,f/d(T) = S0,f/d * e^{(rf-rd)T} -- FX forward price, continuous compounding. */
export function fxForwardPrice(s0: number, rf: number, rd: number, t: number): number {
  return s0 * Math.exp((rf - rd) * t);
}
export const LATEX_FX_FORWARD_PRICE = "F_{0,f/d}(T) = S_{0,f/d}\\,e^{(r_{f} - r_{d})T}";

/** Vt(T) = St,f/d - F0,f/d(T)*e^{-(rf-rd)(T-t)} -- FX forward MTM value. */
export function fxForwardMtm(
  st: number, f0: number, rf: number, rd: number, t: number, valuationTime: number,
): number {
  return st - f0 * Math.exp(-(rf - rd) * (t - valuationTime));
}
export const LATEX_FX_FORWARD_MTM =
  "V_{t}(T) = S_{t,f/d} - F_{0,f/d}(T)e^{-(r_{f} - r_{d})(T-t)}";

// ---------------------------------------------------------------------------
// Section 4 -- Interest rates, FRAs, and futures
// ---------------------------------------------------------------------------

/** DFi = 1/(1+zi)^i -- discount factor for period i at spot rate zi. */
export function discountFactor(zi: number, i: number): number {
  return 1 / Math.pow(1 + zi, i);
}
export const LATEX_DISCOUNT_FACTOR = "DF_{i} = \\frac{1}{(1+z_{i})^{i}}";

/**
 * Solves (1+zA)^A * (1+IFR_{A,B-A})^{B-A} = (1+zB)^B for the implied forward rate
 * between period A and period B (B > A) -- closed-form, no iteration needed.
 */
export function computeDiscountFactors<T extends { period: number; spotRate: number }>(
  curve: T[],
): (T & { discountFactor: number })[] {
  return curve.map((row) => ({ ...row, discountFactor: discountFactor(row.spotRate, row.period) }));
}

export function impliedForwardRate(zA: number, periodA: number, zB: number, periodB: number): number {
  const growthA = Math.pow(1 + zA, periodA);
  const growthB = Math.pow(1 + zB, periodB);
  const periodDiff = periodB - periodA;
  return Math.pow(growthB / growthA, 1 / periodDiff) - 1;
}
export const LATEX_IFR =
  "(1+z_{A})^{A} \\times (1+IFR_{A,B-A})^{B-A} = (1+z_{B})^{B}";

/** (1+APRm/m)^m = (1+APRn/n)^n -- solves for APRn given APRm, compounded m and n times/year. */
export function convertPeriodicity(aprM: number, m: number, n: number): number {
  return n * (Math.pow(1 + aprM / m, m / n) - 1);
}
export const LATEX_PERIODICITY =
  "(1 + \\frac{APR_{m}}{m})^{m} = (1 + \\frac{APR_{n}}{n})^{n}";

/** Net Payment = (MRR_{B-A} - IFR_{A,B-A}) * Notional * Period -- FRA settlement. */
export function fraNetPayment(
  mrr: number, ifr: number, notional: number, period: number,
): number {
  return (mrr - ifr) * notional * period;
}
export const LATEX_FRA_NET_PAYMENT =
  "\\text{Net Payment} = (MRR_{B-A} - IFR_{A,B-A}) \\times \\text{Notional Principal} \\times \\text{Period}";

/** f_{A,B-A} = 100 - (100 * MRR_{A,B-A}) -- interest rate futures price. */
export function irFuturesPrice(mrr: number): number {
  return 100 - 100 * mrr;
}
export const LATEX_IR_FUTURES_PRICE = "f_{A,B-A} = 100 - (100 \\times MRR_{A,B-A})";

/** Contract BPV = Notional Principal * 0.01% * Period -- basis point value. */
export function contractBpv(notional: number, period: number): number {
  return notional * 0.0001 * period;
}
export const LATEX_CONTRACT_BPV =
  "\\text{Contract BPV} = \\text{Notional Principal} \\times 0.01\\% \\times \\text{Period}";

// ---------------------------------------------------------------------------
// Section 5 -- Contingent claims: options payoff and profit
// ---------------------------------------------------------------------------

export function longCallPayoff(sT: number, x: number): number {
  return Math.max(0, sT - x);
}
export function longCallProfit(sT: number, x: number, c0: number): number {
  return longCallPayoff(sT, x) - c0;
}
export const LATEX_LONG_CALL_PAYOFF = "\\text{Payoff} = \\max(0, S_{T} - X)";
export const LATEX_LONG_CALL_PROFIT = "\\Pi = \\max(0, S_{T} - X) - c_{0}";

export function shortCallProfit(sT: number, x: number, c0: number): number {
  return -longCallPayoff(sT, x) + c0;
}
export const LATEX_SHORT_CALL_PROFIT = "\\Pi = -\\max(0, S_{T} - X) + c_{0}";

export function longPutPayoff(sT: number, x: number): number {
  return Math.max(0, x - sT);
}
export function longPutProfit(sT: number, x: number, p0: number): number {
  return longPutPayoff(sT, x) - p0;
}
export const LATEX_LONG_PUT_PAYOFF = "\\text{Payoff} = \\max(0, X - S_{T})";
export const LATEX_LONG_PUT_PROFIT = "\\Pi = \\max(0, X - S_{T}) - p_{0}";

export function shortPutProfit(sT: number, x: number, p0: number): number {
  return -longPutPayoff(sT, x) + p0;
}
export const LATEX_SHORT_PUT_PROFIT = "\\Pi = -\\max(0, X - S_{T}) + p_{0}";

// ---------------------------------------------------------------------------
// Every LaTeX constant above, gathered once so financeFormulas.test.ts can assert each
// one parses cleanly via katex.renderToString without enumerating them by hand twice.
// ---------------------------------------------------------------------------
export const ALL_LATEX_CONSTANTS: Record<string, string> = {
  LATEX_FV_DISCRETE,
  LATEX_FV_CONTINUOUS,
  LATEX_FORWARD_DISCRETE_NO_CF,
  LATEX_FORWARD_DISCRETE_WITH_CF,
  LATEX_FORWARD_CONTINUOUS_CARRY,
  LATEX_PV_OF_FORWARD,
  LATEX_FORWARD_MTM_NO_CF,
  LATEX_FORWARD_MTM_WITH_CF,
  LATEX_FX_FORWARD_PRICE,
  LATEX_FX_FORWARD_MTM,
  LATEX_DISCOUNT_FACTOR,
  LATEX_IFR,
  LATEX_PERIODICITY,
  LATEX_FRA_NET_PAYMENT,
  LATEX_IR_FUTURES_PRICE,
  LATEX_CONTRACT_BPV,
  LATEX_LONG_CALL_PAYOFF,
  LATEX_LONG_CALL_PROFIT,
  LATEX_SHORT_CALL_PROFIT,
  LATEX_LONG_PUT_PAYOFF,
  LATEX_LONG_PUT_PROFIT,
  LATEX_SHORT_PUT_PROFIT,
};
