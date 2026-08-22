import { useMemo, useState } from "react";
import {
  Area, CartesianGrid, ComposedChart, Line, ReferenceDot, ReferenceLine, ResponsiveContainer,
  Tooltip, XAxis, YAxis,
} from "recharts";
import { ConfidenceIntervalBar } from "./ConfidenceIntervalBar";
import { FormulaCard } from "./FormulaCard";
import { Slider } from "./Slider";
import { priceEuropean, type McPriceResult } from "../api";
import {
  longCallPayoff, longCallProfit, shortCallProfit, longPutPayoff, longPutProfit, shortPutProfit,
  LATEX_LONG_CALL_PAYOFF, LATEX_LONG_CALL_PROFIT, LATEX_SHORT_CALL_PROFIT,
  LATEX_LONG_PUT_PAYOFF, LATEX_LONG_PUT_PROFIT, LATEX_SHORT_PUT_PROFIT,
} from "../lib/financeFormulas";

type Position = "long_call" | "short_call" | "long_put" | "short_put";

const POSITIONS: { id: Position; label: string; underlying: "call" | "put" }[] = [
  { id: "long_call", label: "Long call", underlying: "call" },
  { id: "short_call", label: "Short call", underlying: "call" },
  { id: "long_put", label: "Long put", underlying: "put" },
  { id: "short_put", label: "Short put", underlying: "put" },
];

const num = (v: number) => v.toFixed(4);
const pct = (v: number) => `${(v * 100).toFixed(2)}%`;
const yrs = (v: number) => `${v.toFixed(2)}y`;

function profitFor(position: Position, sT: number, x: number, premium: number): number {
  switch (position) {
    case "long_call": return longCallProfit(sT, x, premium);
    case "short_call": return shortCallProfit(sT, x, premium);
    case "long_put": return longPutProfit(sT, x, premium);
    case "short_put": return shortPutProfit(sT, x, premium);
  }
}
function payoffFor(position: Position, sT: number, x: number): number {
  return position === "long_call" || position === "short_call" ? longCallPayoff(sT, x) : longPutPayoff(sT, x);
}
function latexFor(position: Position): { payoff: string; profit: string } {
  switch (position) {
    case "long_call": return { payoff: LATEX_LONG_CALL_PAYOFF, profit: LATEX_LONG_CALL_PROFIT };
    case "short_call": return { payoff: LATEX_LONG_CALL_PAYOFF, profit: LATEX_SHORT_CALL_PROFIT };
    case "long_put": return { payoff: LATEX_LONG_PUT_PAYOFF, profit: LATEX_LONG_PUT_PROFIT };
    case "short_put": return { payoff: LATEX_LONG_PUT_PAYOFF, profit: LATEX_SHORT_PUT_PROFIT };
  }
}
function breakevenFor(position: Position, x: number, premium: number): number {
  return position === "long_call" || position === "short_call" ? x + premium : x - premium;
}
function glossFor(position: Position): string {
  switch (position) {
    case "long_call": return "Unlimited upside above the strike, capped loss equal to the premium paid if the option expires worthless.";
    case "short_call": return "Capped gain equal to the premium collected, unlimited loss if the underlying rallies past the strike.";
    case "long_put": return "Gains as the underlying falls below the strike, capped loss equal to the premium paid if it expires worthless.";
    case "short_put": return "Capped gain equal to the premium collected, large loss if the underlying falls well below the strike.";
  }
}

const tooltipStyle = {
  background: "#161c28", border: "1px solid #212838", borderRadius: 8,
  fontFamily: "'IBM Plex Mono', monospace", fontSize: 12, color: "#f2f4f8",
};
const axisStyle = { fill: "#7c8494", fontSize: 11, fontFamily: "'IBM Plex Mono', monospace" };

// Section 5 (options payoff/profit). The "C++ Backend Bridge" reuses the existing,
// already-deployed, already-tested priceEuropean() Monte Carlo endpoint: fetching a real
// premium overwrites the theoretical premium slider with the engine's actual simulated
// result, so the profit curve immediately reflects real evidence rather than a
// hypothetical number -- no new backend route needed (see docs/design/13-cfa-education-
// dashboard.md for why this was chosen over adding a closed-form Lambda route).
export function OptionsPayoffLab() {
  const [position, setPosition] = useState<Position>("long_call");
  const [x, setX] = useState(100);
  const [sT, setST] = useState(105);
  const [premium, setPremium] = useState(5);

  const [s0, setS0] = useState(100);
  const [rate, setRate] = useState(0.05);
  const [vol, setVol] = useState(0.2);
  const [time, setTime] = useState(1);
  const [fetching, setFetching] = useState(false);
  const [fetchError, setFetchError] = useState<string | null>(null);
  const [fetched, setFetched] = useState<McPriceResult | null>(null);

  const latex = latexFor(position);
  const currentPayoff = payoffFor(position, sT, x);
  const currentProfit = profitFor(position, sT, x, premium);
  const breakeven = breakevenFor(position, x, premium);

  const chartData = useMemo(() => {
    const lo = x * 0.5, hi = x * 1.5;
    const points = [];
    for (let i = 0; i <= 40; i++) {
      const sTSample = lo + ((hi - lo) * i) / 40;
      const profit = profitFor(position, sTSample, x, premium);
      points.push({
        sT: sTSample,
        payoff: payoffFor(position, sTSample, x),
        profit,
        pos: [0, Math.max(profit, 0)],
        neg: [Math.min(profit, 0), 0],
      });
    }
    return points;
  }, [position, x, premium]);

  async function fetchRealPremium() {
    setFetching(true);
    setFetchError(null);
    try {
      const underlying = POSITIONS.find((p) => p.id === position)!.underlying;
      const result = await priceEuropean({
        spot: s0, strike: x, rate, carry_yield: 0, vol, time,
        type: underlying, path_count: 200_000, seed: 42,
      });
      setFetched(result);
      setPremium(result.price);
    } catch (e) {
      setFetchError(e instanceof Error ? e.message : String(e));
    } finally {
      setFetching(false);
    }
  }

  return (
    <section>
      <h2>Options Payoff &amp; Profit</h2>
      <p>
        Payoff and profit at expiration for a single option position. Adjust the strike,
        spot at maturity, and premium to explore the theoretical diagram, or fetch a real
        premium from the live Monte Carlo engine below to replace the theoretical premium
        with actual simulated evidence.
      </p>

      <div className="card">
        <div className="controls">
          <div className="field">
            <label htmlFor="position-select">Position</label>
            <select id="position-select" value={position} onChange={(e) => setPosition(e.target.value as Position)}>
              {POSITIONS.map((p) => <option key={p.id} value={p.id}>{p.label}</option>)}
            </select>
          </div>
        </div>
        <div className="slider-grid">
          <Slider label="Strike (X)" value={x} min={10} max={300} step={1} onChange={setX} format={num} />
          <Slider label="Spot at maturity (ST)" value={sT} min={x * 0.5} max={x * 1.5} step={1} onChange={setST} format={num} />
          <Slider label={position.includes("call") ? "Call premium (c0)" : "Put premium (p0)"} value={premium} min={0} max={50} step={0.1} onChange={setPremium} format={num} />
        </div>
      </div>

      <FormulaCard chip={`${POSITIONS.find((p) => p.id === position)!.label} -- payoff`} latex={latex.payoff} gloss={glossFor(position)}>
        <div className="stat-row">
          <div className="stat">
            <span className="label">Payoff at ST</span>
            <span className="value">{num(currentPayoff)}</span>
          </div>
        </div>
      </FormulaCard>

      <FormulaCard chip={`${POSITIONS.find((p) => p.id === position)!.label} -- profit`} latex={latex.profit} gloss="Profit nets the premium already paid or received against the payoff -- the breakeven is where this line crosses zero.">
        <div className="stat-row">
          <div className="stat stat-hero">
            <span className="label">Profit at ST</span>
            <span className="value">{num(currentProfit)}</span>
          </div>
          <div className="stat">
            <span className="label">Breakeven ST</span>
            <span className="value">{num(breakeven)}</span>
          </div>
        </div>

        <ResponsiveContainer width="100%" height={300}>
          <ComposedChart data={chartData} margin={{ top: 10, right: 12, left: 0, bottom: 0 }}>
            <CartesianGrid stroke="#212838" strokeDasharray="3 3" />
            <XAxis dataKey="sT" type="number" domain={["auto", "auto"]} tick={axisStyle} stroke="#212838" />
            <YAxis tick={axisStyle} stroke="#212838" />
            <Tooltip contentStyle={tooltipStyle} labelStyle={{ color: "#7c8494" }} />
            <ReferenceLine y={0} stroke="#7c8494" strokeDasharray="2 2" />
            <ReferenceLine x={breakeven} stroke="#7c8494" strokeDasharray="2 2" label={{ value: "breakeven", position: "insideTopLeft", fill: "#7c8494", fontSize: 11 }} />
            <Area dataKey="pos" stroke="none" fill="rgba(52,211,153,0.22)" isAnimationActive={false} legendType="none" />
            <Area dataKey="neg" stroke="none" fill="rgba(248,113,113,0.22)" isAnimationActive={false} legendType="none" />
            <Line type="monotone" dataKey="payoff" stroke="#38bdf8" strokeWidth={2} dot={false} isAnimationActive={false} name="Payoff" />
            <Line type="monotone" dataKey="profit" stroke="#e8a33d" strokeWidth={2.5} dot={false} isAnimationActive={false} name="Profit" />
            <ReferenceDot x={sT} y={currentProfit} r={6} fill="#34d399" stroke="#0a0e14" strokeWidth={2} />
          </ComposedChart>
        </ResponsiveContainer>
      </FormulaCard>

      <h3>Fetch a real premium from the engine</h3>
      <div className="card">
        <p style={{ marginTop: 0 }}>
          Uses the live, deployed Monte Carlo European pricer -- the same engine behind
          the Live Pricing tab -- with the strike above and the current-market inputs
          below.
        </p>
        <div className="slider-grid">
          <Slider label="Current spot (S0)" value={s0} min={10} max={300} step={1} onChange={setS0} format={num} />
          <Slider label="Risk-free rate (r)" value={rate} min={-0.02} max={0.15} step={0.0025} onChange={setRate} format={pct} />
          <Slider label="Volatility (sigma)" value={vol} min={0.05} max={1} step={0.01} onChange={setVol} format={pct} />
          <Slider label="Time to expiry (T)" value={time} min={0.0833} max={3} step={0.0833} onChange={setTime} format={yrs} />
        </div>
        <button className="primary" onClick={fetchRealPremium} disabled={fetching}>
          {fetching && <span className="spinner" />}
          {fetching ? "Pricing..." : "Fetch real premium"}
        </button>
        {fetchError && <p className="error">{fetchError}</p>}
        {fetched && (
          <>
            <div className="stat-row">
              <div className="stat stat-hero">
                <span className="label">Real simulated premium (now applied above)</span>
                <span className="value">{num(fetched.price)}</span>
              </div>
              <div className="stat">
                <span className="label">Standard error</span>
                <span className="value">{num(fetched.standard_error)}</span>
              </div>
              <div className="stat">
                <span className="label">Path count</span>
                <span className="value">{fetched.path_count.toLocaleString()}</span>
              </div>
            </div>
            <ConfidenceIntervalBar low={fetched.ci_95_low} high={fetched.ci_95_high} value={fetched.price} format={num} />
          </>
        )}
      </div>
    </section>
  );
}
