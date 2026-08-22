import { describe, it, expect } from "vitest";
import katex from "katex";
import * as F from "./financeFormulas";

describe("Section 1 -- cost of carry", () => {
  it("futureValueDiscrete matches PV(1+r)^N", () => {
    expect(F.futureValueDiscrete(100, 0.05, 3)).toBeCloseTo(100 * 1.05 ** 3, 10);
  });

  it("futureValueContinuous matches PV*e^(rT)", () => {
    expect(F.futureValueContinuous(100, 0.05, 2)).toBeCloseTo(100 * Math.exp(0.1), 10);
  });

  it("forwardPriceDiscreteNoCashFlows matches S0(1+r)^T", () => {
    expect(F.forwardPriceDiscreteNoCashFlows(100, 0.05, 1)).toBeCloseTo(105, 10);
  });

  it("forwardPriceDiscreteWithCashFlows nets income against cost before compounding", () => {
    // S0=100, PV(I)=5, PV(C)=2, r=5%, T=1 -> (100-5+2)*1.05 = 101.85
    expect(F.forwardPriceDiscreteWithCashFlows(100, 5, 2, 0.05, 1)).toBeCloseTo(101.85, 10);
  });

  it("forwardPriceContinuousCarry matches S0*e^((r+c-i)T)", () => {
    expect(F.forwardPriceContinuousCarry(100, 0.05, 0.01, 0.02, 1)).toBeCloseTo(
      100 * Math.exp(0.05 + 0.01 - 0.02), 10,
    );
  });
});

describe("Section 2 -- forward/futures MTM", () => {
  it("presentValueOfForwardPrice discounts back from T to t", () => {
    // F0=110, r=5%, T=1, t=0.5 -> 110 * 1.05^-0.5
    expect(F.presentValueOfForwardPrice(110, 0.05, 1, 0.5)).toBeCloseTo(110 * 1.05 ** -0.5, 10);
  });

  it("forwardMtmNoCashFlows is St minus the discounted forward price", () => {
    const pv = F.presentValueOfForwardPrice(110, 0.05, 1, 0.5);
    expect(F.forwardMtmNoCashFlows(108, 110, 0.05, 1, 0.5)).toBeCloseTo(108 - pv, 10);
  });

  it("forwardMtmWithCashFlows nets PV(I)/PV(C) against St before subtracting the discounted forward", () => {
    const pv = F.presentValueOfForwardPrice(110, 0.05, 1, 0.5);
    expect(F.forwardMtmWithCashFlows(108, 3, 1, 110, 0.05, 1, 0.5)).toBeCloseTo(
      (108 - 3 + 1) - pv, 10,
    );
  });

  it("MTM value is exactly zero at initiation for a fairly-priced forward", () => {
    const f0 = F.forwardPriceDiscreteNoCashFlows(100, 0.05, 1);
    expect(F.forwardMtmNoCashFlows(100, f0, 0.05, 1, 0)).toBeCloseTo(0, 10);
  });
});

describe("Section 3 -- FX forwards", () => {
  it("fxForwardPrice matches covered interest rate parity", () => {
    expect(F.fxForwardPrice(1.1, 0.03, 0.05, 1)).toBeCloseTo(1.1 * Math.exp(0.03 - 0.05), 10);
  });

  it("fxForwardMtm is zero at initiation for a fairly-priced FX forward", () => {
    const f0 = F.fxForwardPrice(1.1, 0.03, 0.05, 1);
    expect(F.fxForwardMtm(1.1, f0, 0.03, 0.05, 1, 0)).toBeCloseTo(0, 10);
  });
});

describe("Section 4 -- interest rates, FRAs, futures", () => {
  it("discountFactor matches 1/(1+z)^i", () => {
    expect(F.discountFactor(0.04, 2)).toBeCloseTo(1 / 1.04 ** 2, 10);
  });

  it("impliedForwardRate satisfies the defining no-arbitrage identity", () => {
    const zA = 0.03, periodA = 1, zB = 0.04, periodB = 2;
    const ifr = F.impliedForwardRate(zA, periodA, zB, periodB);
    const lhs = (1 + zA) ** periodA * (1 + ifr) ** (periodB - periodA);
    const rhs = (1 + zB) ** periodB;
    expect(lhs).toBeCloseTo(rhs, 10);
  });

  it("computeDiscountFactors annotates every curve row", () => {
    const curve = [{ period: 1, spotRate: 0.03 }, { period: 2, spotRate: 0.04 }];
    const withDf = F.computeDiscountFactors(curve);
    expect(withDf[0].discountFactor).toBeCloseTo(F.discountFactor(0.03, 1), 10);
    expect(withDf[1].discountFactor).toBeCloseTo(F.discountFactor(0.04, 2), 10);
  });

  it("convertPeriodicity satisfies the defining periodicity identity", () => {
    const aprM = 0.06, m = 2, n = 12;
    const aprN = F.convertPeriodicity(aprM, m, n);
    const lhs = (1 + aprM / m) ** m;
    const rhs = (1 + aprN / n) ** n;
    expect(lhs).toBeCloseTo(rhs, 10);
  });

  it("fraNetPayment is the rate differential times notional times period", () => {
    expect(F.fraNetPayment(0.05, 0.045, 1_000_000, 0.25)).toBeCloseTo(
      (0.05 - 0.045) * 1_000_000 * 0.25, 10,
    );
  });

  it("irFuturesPrice matches 100 - 100*MRR", () => {
    expect(F.irFuturesPrice(0.0325)).toBeCloseTo(96.75, 10);
  });

  it("contractBpv matches notional * 0.01% * period", () => {
    expect(F.contractBpv(1_000_000, 0.25)).toBeCloseTo(1_000_000 * 0.0001 * 0.25, 10);
  });
});

describe("Section 5 -- options payoff and profit", () => {
  it("long call payoff/profit", () => {
    expect(F.longCallPayoff(110, 100)).toBe(10);
    expect(F.longCallPayoff(90, 100)).toBe(0);
    expect(F.longCallProfit(110, 100, 4)).toBe(6);
  });

  it("short call profit is the mirror image of long call profit", () => {
    expect(F.shortCallProfit(110, 100, 4)).toBeCloseTo(-F.longCallProfit(110, 100, 4), 10);
  });

  it("long put payoff/profit", () => {
    expect(F.longPutPayoff(90, 100)).toBe(10);
    expect(F.longPutPayoff(110, 100)).toBe(0);
    expect(F.longPutProfit(90, 100, 3)).toBe(7);
  });

  it("short put profit is the mirror image of long put profit", () => {
    expect(F.shortPutProfit(90, 100, 3)).toBeCloseTo(-F.longPutProfit(90, 100, 3), 10);
  });
});

describe("Every exported LaTeX constant is valid KaTeX", () => {
  for (const [name, latex] of Object.entries(F.ALL_LATEX_CONSTANTS)) {
    it(`${name} parses without throwing`, () => {
      expect(() => katex.renderToString(latex, { throwOnError: true })).not.toThrow();
    });
  }
});
