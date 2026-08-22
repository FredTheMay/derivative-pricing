import { useMemo, useState } from "react";
import {
  CartesianGrid, Legend, Line, LineChart, ReferenceDot, ReferenceLine, ResponsiveContainer,
  Tooltip, XAxis, YAxis,
} from "recharts";
import { FormulaCard } from "./FormulaCard";
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

const tooltipStyle = {
  background: "#161c28", border: "1px solid #212838", borderRadius: 8,
  fontFamily: "'IBM Plex Mono', monospace", fontSize: 12, color: "#f2f4f8",
};
const axisStyle = { fill: "#7c8494", fontSize: 11, fontFamily: "'IBM Plex Mono', monospace" };

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

  const termStructure = useMemo(() => {
    const points = [];
    for (let i = 0; i <= 40; i++) {
      const sweptT = 0.1 + ((5 - 0.1) * i) / 40;
      points.push({
        t: sweptT,
        noCf: forwardPriceDiscreteNoCashFlows(s0, r, sweptT),
        withCf: forwardPriceDiscreteWithCashFlows(s0, pvIncome, pvCost, r, sweptT),
        carry: forwardPriceContinuousCarry(s0, r, cost, income, sweptT),
      });
    }
    return points;
  }, [s0, r, pvIncome, pvCost, income, cost]);

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

      <FormulaCard
        split
        chip="Discrete compounding"
        latex={LATEX_FV_DISCRETE}
        gloss="A dollar invested today grows by a fixed rate applied once per period -- the everyday compound-interest formula."
      >
        <div className="stat-row">
          <div className="stat">
            <span className="label">FV (discrete, N=T periods)</span>
            <span className="value">{num(futureValueDiscrete(s0, r, t))}</span>
          </div>
        </div>
      </FormulaCard>

      <FormulaCard
        split
        chip="Continuous compounding"
        latex={LATEX_FV_CONTINUOUS}
        gloss="The limit of compounding infinitely often -- growth is applied continuously rather than at fixed intervals."
      >
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
      </FormulaCard>

      <FormulaCard
        split
        chip="Forward price -- no cash flows"
        latex={LATEX_FORWARD_DISCRETE_NO_CF}
        gloss="With no income or storage cost, the fair forward price is just the spot price grown at the risk-free rate."
      >
        <div className="stat-row">
          <div className="stat stat-hero">
            <span className="label">F&#8320;(T)</span>
            <span className="value">{num(forwardPriceDiscreteNoCashFlows(s0, r, t))}</span>
          </div>
        </div>
      </FormulaCard>

      <FormulaCard
        split
        chip="Forward price -- discrete costs &amp; benefits"
        latex={LATEX_FORWARD_DISCRETE_WITH_CF}
        gloss="Income the asset pays out (dividends, coupons) is netted against the spot price before growing it forward; storage or carry costs add to it."
      >
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
      </FormulaCard>

      <FormulaCard
        split
        chip="Forward price -- continuous carry"
        latex={LATEX_FORWARD_CONTINUOUS_CARRY}
        gloss="The same income/cost tradeoff as above, expressed as continuous annualized rates instead of one-time present values."
      >
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
      </FormulaCard>

      <h3>Forward price as maturity stretches out</h3>
      <div className="card">
        <p style={{ marginTop: 0 }}>
          All three formulas agree at T=0 (the forward price collapses to spot) and diverge
          as maturity lengthens -- carry costs and income pull the curves apart.
        </p>
        <ResponsiveContainer width="100%" height={280}>
          <LineChart data={termStructure} margin={{ top: 10, right: 16, left: 0, bottom: 20 }}>
            <CartesianGrid stroke="#212838" strokeDasharray="3 3" />
            <XAxis dataKey="t" type="number" domain={["auto", "auto"]} tick={axisStyle} stroke="#212838"
                   label={{ value: "time to maturity (T)", position: "bottom", offset: 0, fill: "#7c8494", fontSize: 11 }} />
            <YAxis tick={axisStyle} stroke="#212838" width={56} />
            <Tooltip contentStyle={tooltipStyle} labelStyle={{ color: "#7c8494" }} />
            <Legend verticalAlign="top" height={28} wrapperStyle={{ fontSize: 11, fontFamily: "'IBM Plex Mono', monospace" }} />
            <ReferenceLine y={s0} stroke="#7c8494" strokeDasharray="2 2" label={{ value: "S0", position: "insideTopLeft", fill: "#7c8494", fontSize: 11 }} />
            <Line type="monotone" dataKey="noCf" name="No cash flows" stroke="#e8a33d" strokeWidth={2} dot={false}
                  isAnimationActive animationDuration={250} animationEasing="ease-out" />
            <Line type="monotone" dataKey="withCf" name="Discrete costs/benefits" stroke="#38bdf8" strokeWidth={2} dot={false}
                  isAnimationActive animationDuration={250} animationEasing="ease-out" />
            <Line type="monotone" dataKey="carry" name="Continuous carry" stroke="#34d399" strokeWidth={2} dot={false}
                  isAnimationActive animationDuration={250} animationEasing="ease-out" />
            <ReferenceDot x={t} y={forwardPriceDiscreteNoCashFlows(s0, r, t)} r={4} fill="#e8a33d" stroke="#0a0e14" strokeWidth={1} className="chart-marker" isFront />
          </LineChart>
        </ResponsiveContainer>
      </div>
    </section>
  );
}
