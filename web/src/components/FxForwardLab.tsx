import { useMemo, useState } from "react";
import {
  Area, CartesianGrid, ComposedChart, Legend, Line, ReferenceDot, ReferenceLine,
  ResponsiveContainer, Tooltip, XAxis, YAxis,
} from "recharts";
import { FormulaCard } from "./FormulaCard";
import { Slider } from "./Slider";
import {
  fxForwardPrice, fxForwardMtm, LATEX_FX_FORWARD_PRICE, LATEX_FX_FORWARD_MTM,
} from "../lib/financeFormulas";

const pct = (v: number) => `${(v * 100).toFixed(2)}%`;
const num = (v: number) => v.toFixed(4);
const yrs = (v: number) => `${v.toFixed(2)}y`;

const tooltipStyle = {
  background: "#161c28", border: "1px solid #212838", borderRadius: 8,
  fontFamily: "'IBM Plex Mono', monospace", fontSize: 12, color: "#f2f4f8",
};
const axisStyle = { fill: "#7c8494", fontSize: 11, fontFamily: "'IBM Plex Mono', monospace" };

// Section 3 (FX forwards). Same covered-interest-rate-parity shape as Section 1's cost
// of carry, but the "carry rate" is the foreign/domestic interest rate differential
// (rf - rd) instead of a single asset's income/cost -- highlighted directly below by
// showing that differential as its own stat, and by shading the gap between spot and
// forward green (premium) or red (discount) on the term-structure chart.
export function FxForwardLab() {
  const [s0, setS0] = useState(1.1);
  const [rf, setRf] = useState(0.03);
  const [rd, setRd] = useState(0.05);
  const [maturity, setMaturity] = useState(1);
  const [t, setT] = useState(0.5);
  const [st, setSt] = useState(1.12);

  const f0 = fxForwardPrice(s0, rf, rd, maturity);
  const vt = fxForwardMtm(st, f0, rf, rd, maturity, t);
  const isPremium = f0 > s0 + 1e-9;
  const isDiscount = f0 < s0 - 1e-9;

  const curve = useMemo(() => {
    const points = [];
    for (let i = 0; i <= 40; i++) {
      const sweptT = 0.1 + ((5 - 0.1) * i) / 40;
      const forward = fxForwardPrice(s0, rf, rd, sweptT);
      points.push({
        t: sweptT,
        forward,
        premiumRange: forward >= s0 ? [s0, forward] : [s0, s0],
        discountRange: forward < s0 ? [forward, s0] : [s0, s0],
      });
    }
    return points;
  }, [s0, rf, rd]);

  return (
    <section>
      <h2>FX Forwards</h2>
      <p>
        Covered interest rate parity: the forward price is set entirely by the interest
        rate differential between the foreign (r<sub>f</sub>) and domestic (r<sub>d</sub>)
        rates -- no view on the exchange rate's direction is needed.
      </p>

      <div className="card">
        <div className="slider-grid">
          <Slider label="Spot S0,f/d" value={s0} min={0.5} max={2} step={0.01} onChange={setS0} format={num} />
          <Slider label="Foreign rate (rf)" value={rf} min={-0.02} max={0.15} step={0.0025} onChange={setRf} format={pct} />
          <Slider label="Domestic rate (rd)" value={rd} min={-0.02} max={0.15} step={0.0025} onChange={setRd} format={pct} />
          <Slider label="Maturity (T)" value={maturity} min={0.25} max={5} step={0.25} onChange={setMaturity} format={yrs} />
        </div>
        <div className="stat-row">
          <div className="stat">
            <span className="label">Rate differential (rf &minus; rd)</span>
            <span className="value">{pct(rf - rd)}</span>
          </div>
        </div>
      </div>

      <FormulaCard
        chip="FX forward price"
        latex={LATEX_FX_FORWARD_PRICE}
        gloss="The currency with the higher interest rate trades at a forward discount, and the lower-rate currency trades at a forward premium -- otherwise riskless arbitrage would be possible."
      >
        <div className="stat-row">
          <div className="stat stat-hero">
            <span className="label">F0,f/d(T)</span>
            <span className="value">{num(f0)}</span>
          </div>
          <div className="stat">
            <span className="label">vs. spot</span>
            <span className="value">
              {isPremium && <span className="pill good">Forward premium</span>}
              {isDiscount && <span className="pill bad">Forward discount</span>}
              {!isPremium && !isDiscount && <span className="pill neutral">At parity</span>}
            </span>
          </div>
        </div>

        <ResponsiveContainer width="100%" height={260}>
          <ComposedChart data={curve} margin={{ top: 10, right: 16, left: 0, bottom: 20 }}>
            <CartesianGrid stroke="#212838" strokeDasharray="3 3" />
            <XAxis dataKey="t" type="number" domain={["auto", "auto"]} tick={axisStyle} stroke="#212838"
                   label={{ value: "time to maturity (T)", position: "bottom", offset: 0, fill: "#7c8494", fontSize: 11 }} />
            <YAxis tick={axisStyle} stroke="#212838" width={56} domain={["auto", "auto"]} />
            <Tooltip contentStyle={tooltipStyle} labelStyle={{ color: "#7c8494" }} />
            <Legend verticalAlign="top" height={28} wrapperStyle={{ fontSize: 11, fontFamily: "'IBM Plex Mono', monospace" }} />
            <ReferenceLine y={s0} stroke="#7c8494" strokeDasharray="2 2" label={{ value: "spot", position: "insideTopLeft", fill: "#7c8494", fontSize: 11 }} />
            <Area dataKey="premiumRange" name="Forward premium" stroke="none" fill="rgba(52,211,153,0.22)"
                  isAnimationActive animationDuration={250} animationEasing="ease-out" legendType="none" />
            <Area dataKey="discountRange" name="Forward discount" stroke="none" fill="rgba(248,113,113,0.22)"
                  isAnimationActive animationDuration={250} animationEasing="ease-out" legendType="none" />
            <Line type="monotone" dataKey="forward" name="Forward price" stroke="#e8a33d" strokeWidth={2} dot={false}
                  isAnimationActive animationDuration={250} animationEasing="ease-out" />
            <ReferenceDot x={maturity} y={f0} r={4} fill="#e8a33d" stroke="#0a0e14" strokeWidth={1} className="chart-marker" isFront />
          </ComposedChart>
        </ResponsiveContainer>
      </FormulaCard>

      <FormulaCard
        split
        chip="Mark-to-market before expiration"
        latex={LATEX_FX_FORWARD_MTM}
        gloss="As spot moves and time passes, the contract's value drifts away from zero -- this is the P&amp;L a holder of the forward would realize by unwinding it today."
      >
        <div className="slider-grid">
          <Slider label="Valuation date (t)" value={t} min={0} max={maturity} step={maturity / 40} onChange={setT} format={yrs} />
          <Slider label="Current spot St,f/d" value={st} min={s0 * 0.7} max={s0 * 1.3} step={0.01} onChange={setSt} format={num} />
        </div>
        <div className="stat-row">
          <div className="stat stat-hero">
            <span className="label">Vt(T)</span>
            <span className="value">{num(vt)}</span>
          </div>
        </div>
      </FormulaCard>
    </section>
  );
}
