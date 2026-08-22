import { useState } from "react";
import { Katex } from "./Katex";
import { Slider } from "./Slider";
import {
  futureValueDiscrete, futureValueContinuous,
  forwardPriceDiscreteNoCashFlows, forwardPriceDiscreteWithCashFlows,
  forwardPriceContinuousCarry,
  LATEX_FV_DISCRETE, LATEX_FV_CONTINUOUS,
  LATEX_FORWARD_DISCRETE_NO_CF, LATEX_FORWARD_DISCRETE_WITH_CF, LATEX_FORWARD_CONTINUOUS_CARRY,
} from "../lib/financeFormulas";

const pct = (v: number) => `${(v * 100).toFixed(2)}%`;
const num = (v: number) => v.toFixed(4);
const yrs = (v: number) => `${v.toFixed(2)}y`;

// Section 1 (cost of carry). Every formula below is deterministic time-value-of-money
// arithmetic -- no simulation, no network call -- so every slider recomputes every
// formula's displayed value on the same frame it moves. Sections 1-4 are intentionally
// self-contained TypeScript, independent of the C++ engine's own (continuous-only)
// analytic pricers -- see financeFormulas.ts's header comment.
export function ForwardPricingLab() {
  const [s0, setS0] = useState(100);
  const [r, setR] = useState(0.05);
  const [t, setT] = useState(1);
  const [pvIncome, setPvIncome] = useState(3);
  const [pvCost, setPvCost] = useState(1);
  const [income, setIncome] = useState(0.02);
  const [cost, setCost] = useState(0.01);

  return (
    <section>
      <h2>Cost of Carry &amp; Forward Pricing</h2>
      <p>
        Every value below is computed live, client-side, the instant a slider moves --
        no network call. Move S&#8320;, r, or T and watch every formula's result update
        together.
      </p>

      <div className="card">
        <div className="slider-grid">
          <Slider label="Spot / PV (S0)" value={s0} min={10} max={300} step={1} onChange={setS0} format={num} />
          <Slider label="Risk-free rate (r)" value={r} min={-0.02} max={0.15} step={0.0025} onChange={setR} format={pct} />
          <Slider label="Time to maturity (T)" value={t} min={0.25} max={5} step={0.25} onChange={setT} format={yrs} />
        </div>
      </div>

      <h3>Discrete vs. continuous compounding</h3>
      <div className="card">
        <Katex latex={LATEX_FV_DISCRETE} />
        <div className="stat-row">
          <div className="stat">
            <span className="label">FV (discrete, N=T periods)</span>
            <span className="value">{num(futureValueDiscrete(s0, r, t))}</span>
          </div>
        </div>
        <Katex latex={LATEX_FV_CONTINUOUS} />
        <div className="stat-row">
          <div className="stat">
            <span className="label">FV (continuous)</span>
            <span className="value">{num(futureValueContinuous(s0, r, t))}</span>
          </div>
          <div className="stat">
            <span className="label">Continuous &minus; discrete</span>
            <span className="value">
              {num(futureValueContinuous(s0, r, t) - futureValueDiscrete(s0, r, t))}
            </span>
          </div>
        </div>
      </div>

      <h3>Forward price &mdash; no additional cash flows</h3>
      <div className="card">
        <Katex latex={LATEX_FORWARD_DISCRETE_NO_CF} />
        <div className="stat-row">
          <div className="stat stat-hero">
            <span className="label">F&#8320;(T)</span>
            <span className="value">{num(forwardPriceDiscreteNoCashFlows(s0, r, t))}</span>
          </div>
        </div>
      </div>

      <h3>Forward price &mdash; known discrete costs and benefits</h3>
      <div className="card">
        <Katex latex={LATEX_FORWARD_DISCRETE_WITH_CF} />
        <div className="slider-grid">
          <Slider label="PV0(I) -- income" value={pvIncome} min={0} max={20} step={0.5} onChange={setPvIncome} format={num} />
          <Slider label="PV0(C) -- cost" value={pvCost} min={0} max={20} step={0.5} onChange={setPvCost} format={num} />
        </div>
        <div className="stat-row">
          <div className="stat stat-hero">
            <span className="label">F&#8320;(T)</span>
            <span className="value">
              {num(forwardPriceDiscreteWithCashFlows(s0, pvIncome, pvCost, r, t))}
            </span>
          </div>
        </div>
      </div>

      <h3>Forward price &mdash; continuous income &amp; cost rates</h3>
      <div className="card">
        <Katex latex={LATEX_FORWARD_CONTINUOUS_CARRY} />
        <div className="slider-grid">
          <Slider label="Income rate (i)" value={income} min={-0.05} max={0.1} step={0.0025} onChange={setIncome} format={pct} />
          <Slider label="Cost rate (c)" value={cost} min={-0.05} max={0.1} step={0.0025} onChange={setCost} format={pct} />
        </div>
        <div className="stat-row">
          <div className="stat stat-hero">
            <span className="label">F&#8320;(T)</span>
            <span className="value">{num(forwardPriceContinuousCarry(s0, r, cost, income, t))}</span>
          </div>
        </div>
      </div>
    </section>
  );
}
