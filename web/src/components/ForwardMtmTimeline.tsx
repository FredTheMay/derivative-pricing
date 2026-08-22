import { useMemo, useState } from "react";
import {
  Area, CartesianGrid, ComposedChart, ReferenceDot, ReferenceLine, ResponsiveContainer,
  Tooltip, XAxis, YAxis,
} from "recharts";
import { FormulaCard } from "./FormulaCard";
import { Slider } from "./Slider";
import {
  forwardPriceDiscreteNoCashFlows, presentValueOfForwardPrice, forwardMtmNoCashFlows,
  LATEX_PV_OF_FORWARD, LATEX_FORWARD_MTM_NO_CF,
} from "../lib/financeFormulas";

const num = (v: number) => v.toFixed(4);
const yrs = (v: number) => `${v.toFixed(2)}y`;
const pct = (v: number) => `${(v * 100).toFixed(2)}%`;

const tooltipStyle = {
  background: "#161c28", border: "1px solid #212838", borderRadius: 8,
  fontFamily: "'IBM Plex Mono', monospace", fontSize: 12, color: "#f2f4f8",
};
const axisStyle = { fill: "#7c8494", fontSize: 11, fontFamily: "'IBM Plex Mono', monospace" };

// Section 2 (forward/futures MTM). The forward's terms (S0, r, T) are fixed at
// initiation via their own sliders, from which F0 is derived; the "scrubbable timeline"
// is two independent sliders -- t (valuation date) and St (current spot) -- since Vt is
// linear in St at any fixed t (slope 1, per the exact formula), the chart redraws
// instantly as a straight line whose intercept shifts as t is scrubbed toward T (the
// discounting adjustment shrinking to zero), rather than inventing a fake "realistic"
// price path.
export function ForwardMtmTimeline() {
  const [s0, setS0] = useState(100);
  const [r, setR] = useState(0.05);
  const [maturity, setMaturity] = useState(1);
  const [t, setT] = useState(0.5);
  const [st, setSt] = useState(102);

  const f0 = forwardPriceDiscreteNoCashFlows(s0, r, maturity);
  const pvOfF0 = presentValueOfForwardPrice(f0, r, maturity, t);
  const vt = forwardMtmNoCashFlows(st, f0, r, maturity, t);

  const chartData = useMemo(() => {
    const lo = s0 * 0.5, hi = s0 * 1.5;
    const points = [];
    for (let i = 0; i <= 20; i++) {
      const stSample = lo + ((hi - lo) * i) / 20;
      const vtSample = forwardMtmNoCashFlows(stSample, f0, r, maturity, t);
      points.push({
        st: stSample,
        vt: vtSample,
        pos: [0, Math.max(vtSample, 0)],
        neg: [Math.min(vtSample, 0), 0],
      });
    }
    return points;
  }, [s0, f0, r, maturity, t]);

  return (
    <section>
      <h2>Forward &amp; Futures Valuation (Mark-to-Market)</h2>
      <p>
        A forward's terms are fixed at initiation (S&#8320;, r, T below); scrub the
        valuation date <code>t</code> and the current spot <code>S_t</code> to see how
        the mark-to-market value changes prior to expiration.
      </p>

      <div className="card">
        <div className="slider-grid">
          <Slider label="Spot at initiation (S0)" value={s0} min={10} max={300} step={1} onChange={setS0} format={num} />
          <Slider label="Risk-free rate (r)" value={r} min={-0.02} max={0.15} step={0.0025} onChange={setR} format={pct} />
          <Slider label="Maturity (T)" value={maturity} min={0.25} max={5} step={0.25} onChange={setMaturity} format={yrs} />
        </div>
        <div className="stat-row">
          <div className="stat">
            <span className="label">F0(T) locked in at initiation</span>
            <span className="value">{num(f0)}</span>
          </div>
        </div>
      </div>

      <h3>Scrub the timeline</h3>
      <div className="card">
        <div className="slider-grid">
          <Slider label="Valuation date (t)" value={t} min={0} max={maturity} step={maturity / 40} onChange={setT} format={yrs} />
          <Slider label="Current spot (St)" value={st} min={s0 * 0.5} max={s0 * 1.5} step={1} onChange={setSt} format={num} />
        </div>

        <FormulaCard
          split
          chip="PV of the locked-in forward price"
          latex={LATEX_PV_OF_FORWARD}
          gloss="Discounting F0(T) back to today's valuation date t tells you what the original forward price is worth right now, before comparing it to the current spot."
        >
          <div className="stat-row">
            <div className="stat">
              <span className="label">PVt of F0(T)</span>
              <span className="value">{num(pvOfF0)}</span>
            </div>
          </div>
        </FormulaCard>

        <FormulaCard
          chip="Long forward MTM value"
          latex={LATEX_FORWARD_MTM_NO_CF}
          gloss="The contract is worth exactly the gap between where the underlying trades now and the discounted price you locked in -- positive if spot has risen, negative if it has fallen."
        >
          <div className="stat-row">
            <div className="stat stat-hero">
              <span className="label">Vt(T) &mdash; long forward MTM value</span>
              <span className="value">{num(vt)}</span>
            </div>
            <div className="stat">
              <span className="label">Breakeven St</span>
              <span className="value">{num(pvOfF0)}</span>
            </div>
          </div>

          <ResponsiveContainer width="100%" height={280}>
            <ComposedChart data={chartData} margin={{ top: 10, right: 12, left: 0, bottom: 0 }}>
              <CartesianGrid stroke="#212838" strokeDasharray="3 3" />
              <XAxis dataKey="st" type="number" domain={["auto", "auto"]} tick={axisStyle} stroke="#212838" />
              <YAxis tick={axisStyle} stroke="#212838" />
              <Tooltip contentStyle={tooltipStyle} labelStyle={{ color: "#7c8494" }} />
              <ReferenceLine y={0} stroke="#7c8494" strokeDasharray="2 2" />
              <ReferenceLine x={pvOfF0} stroke="#7c8494" strokeDasharray="2 2" label={{ value: "breakeven", position: "insideTopLeft", fill: "#7c8494", fontSize: 11 }} />
              <Area dataKey="pos" stroke="none" fill="rgba(52,211,153,0.22)"
                    isAnimationActive animationDuration={250} animationEasing="ease-out" legendType="none" />
              <Area dataKey="neg" stroke="none" fill="rgba(248,113,113,0.22)"
                    isAnimationActive animationDuration={250} animationEasing="ease-out" legendType="none" />
              <Area type="monotone" dataKey="vt" stroke="#e8a33d" strokeWidth={2.5} fill="none" name="Vt"
                    isAnimationActive animationDuration={250} animationEasing="ease-out" />
              <ReferenceDot x={st} y={vt} r={6} fill="#38bdf8" stroke="#0a0e14" strokeWidth={2} className="chart-marker" isFront />
            </ComposedChart>
          </ResponsiveContainer>
        </FormulaCard>
      </div>
    </section>
  );
}
