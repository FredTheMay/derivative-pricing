import { useMemo, useState, type ReactNode } from "react";
import {
  Area, ComposedChart, Line, LineChart, ReferenceDot, ResponsiveContainer, XAxis, YAxis,
} from "recharts";
import { Slider } from "./Slider";
import { YieldCurveChart } from "./YieldCurveChart";
import {
  forwardPriceContinuousCarry, forwardPriceDiscreteNoCashFlows,
  forwardMtmNoCashFlows, fxForwardPrice, impliedForwardRate, longCallProfit, longPutProfit,
} from "../lib/financeFormulas";

const num = (v: number) => v.toFixed(4);
const pct = (v: number) => `${(v * 100).toFixed(2)}%`;
const yrs = (v: number) => `${v.toFixed(2)}y`;

// A very light axis for the mini-charts -- just the min/max tick, small and dim, so a
// glance gives a sense of scale without turning a sparkline into a full chart.
const lightAxisTick = { fontSize: 9, fill: "#4a5468", fontFamily: "'IBM Plex Mono', monospace" };
const lightAxisLine = { stroke: "#1c2330" };

const DEMO_CURVE = [
  { period: 0.5, spotRate: 0.02 },
  { period: 1, spotRate: 0.025 },
  { period: 1.5, spotRate: 0.028 },
  { period: 2, spotRate: 0.03 },
];

function Icon({ children }: { children: ReactNode }) {
  return (
    <svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor"
         strokeWidth="1.8" strokeLinecap="round" strokeLinejoin="round" aria-hidden="true">
      {children}
    </svg>
  );
}

interface WidgetProps {
  icon: ReactNode;
  title: string;
  onOpenTab: () => void;
  children: ReactNode;
}

function DashboardWidget({ icon, title, onOpenTab, children }: WidgetProps) {
  return (
    <div className="dashboard-card">
      <div className="dashboard-card-head">
        <span className="dashboard-card-title">{icon}{title}</span>
        <button className="ghost" onClick={onOpenTab}>Open full calculator &rarr;</button>
      </div>
      {children}
    </div>
  );
}

interface DashboardProps {
  onOpenTab: (id: string) => void;
}

// A self-contained playground, independent of the 5 Learn calculator tabs -- every widget
// below holds its own local state and imports only pure math from financeFormulas.ts (plus
// the shared Slider/YieldCurveChart primitives). The only coupling to the rest of the app
// is onOpenTab, used purely for "open full calculator" navigation -- no value or computed
// result is ever shared between this component and the calculator tabs. See
// docs/design/14-visual-redesign.md.
export function Dashboard({ onOpenTab }: DashboardProps) {
  // ---- Cost of carry ----
  const [ccS0, setCcS0] = useState(100);
  const [ccT, setCcT] = useState(1);
  const ccRate = 0.05;
  const ccForward = forwardPriceContinuousCarry(ccS0, ccRate, 0, 0, ccT);
  const ccCurve = useMemo(() => {
    const points = [];
    for (let i = 0; i <= 20; i++) {
      const t = 0.1 + ((5 - 0.1) * i) / 20;
      points.push({ t, forward: forwardPriceContinuousCarry(ccS0, ccRate, 0, 0, t) });
    }
    return points;
  }, [ccS0]);
  // Explicit min/max ticks, computed once from the swept data -- Recharts' automatic "nice
  // tick" generation recurses infinitely for some tiny tickCount/domain combinations on
  // this version, so the mini-charts' light axes always pass their own literal tick values
  // instead of relying on tickCount.
  const ccForwardRange: [number, number] = [
    Math.min(...ccCurve.map((p) => p.forward)), Math.max(...ccCurve.map((p) => p.forward)),
  ];

  // ---- Forward MTM ----
  const [mtmT, setMtmT] = useState(0.5);
  const [mtmSt, setMtmSt] = useState(102);
  const mtmS0 = 100, mtmRate = 0.05, mtmMaturity = 1;
  const mtmF0 = forwardPriceDiscreteNoCashFlows(mtmS0, mtmRate, mtmMaturity);
  const mtmVt = forwardMtmNoCashFlows(mtmSt, mtmF0, mtmRate, mtmMaturity, mtmT);
  const mtmCurve = useMemo(() => {
    const points = [];
    for (let i = 0; i <= 20; i++) {
      const st = 50 + ((150 - 50) * i) / 20;
      const vt = forwardMtmNoCashFlows(st, mtmF0, mtmRate, mtmMaturity, mtmT);
      points.push({ st, vt, pos: [0, Math.max(vt, 0)], neg: [Math.min(vt, 0), 0] });
    }
    return points;
  }, [mtmF0, mtmT]);
  const mtmVtRange: [number, number] = [
    Math.min(...mtmCurve.map((p) => p.vt)), Math.max(...mtmCurve.map((p) => p.vt)),
  ];

  // ---- FX forwards ----
  const [fxRf, setFxRf] = useState(0.03);
  const [fxRd, setFxRd] = useState(0.05);
  const fxS0 = 1.1, fxMaturity = 1;
  const fxF0 = fxForwardPrice(fxS0, fxRf, fxRd, fxMaturity);
  const fxIsPremium = fxF0 > fxS0 + 1e-9;
  const fxIsDiscount = fxF0 < fxS0 - 1e-9;

  // ---- Rates & FRAs ----
  const [ifrA, setIfrA] = useState(0.5);
  const [ifrB, setIfrB] = useState(1);
  const rowA = DEMO_CURVE.find((r) => r.period === ifrA);
  const rowB = DEMO_CURVE.find((r) => r.period === ifrB);
  const ifrResult = rowA && rowB && rowB.period > rowA.period
    ? impliedForwardRate(rowA.spotRate, rowA.period, rowB.spotRate, rowB.period)
    : null;

  // ---- Options payoff ----
  const [optPosition, setOptPosition] = useState<"long_call" | "long_put">("long_call");
  const [optST, setOptST] = useState(105);
  const optX = 100, optPremium = 5;
  const optProfit = optPosition === "long_call"
    ? longCallProfit(optST, optX, optPremium)
    : longPutProfit(optST, optX, optPremium);
  const optCurve = useMemo(() => {
    const points = [];
    for (let i = 0; i <= 20; i++) {
      const sT = 50 + ((150 - 50) * i) / 20;
      const profit = optPosition === "long_call"
        ? longCallProfit(sT, optX, optPremium)
        : longPutProfit(sT, optX, optPremium);
      points.push({ sT, profit, pos: [0, Math.max(profit, 0)], neg: [Math.min(profit, 0), 0] });
    }
    return points;
  }, [optPosition]);
  const optProfitRange: [number, number] = [
    Math.min(...optCurve.map((p) => p.profit)), Math.max(...optCurve.map((p) => p.profit)),
  ];

  return (
    <section>
      <h2>Derivative Pricing Engine by Frederic Lemay</h2>
      <p>
        A Monte Carlo derivatives pricing and risk engine built from first principles in
        C++20, paired with an interactive companion for CFA Level I Derivatives concepts.
        Each card below is a self-contained mini-calculator across the five core concepts --
        open the full calculator for every formula and control, or head to Explore in the
        sidebar for live pricing, Greeks surfaces, and performance benchmarking.
      </p>

      <div className="stat-row dashboard-snapshot">
        <div className="stat">
          <span className="label">Forward price F0(T)</span>
          <span className="value">{num(ccForward)}</span>
        </div>
        <div className="stat">
          <span className="label">Forward MTM Vt(T)</span>
          <span className="value">{num(mtmVt)}</span>
        </div>
        <div className="stat">
          <span className="label">FX forward F0,f/d(T)</span>
          <span className="value">{num(fxF0)}</span>
        </div>
        <div className="stat">
          <span className="label">Implied forward rate</span>
          <span className="value">{ifrResult === null ? "--" : pct(ifrResult)}</span>
        </div>
        <div className="stat">
          <span className="label">Option profit at ST</span>
          <span className="value">{num(optProfit)}</span>
        </div>
      </div>

      <div className="dashboard-grid">
        <DashboardWidget
          icon={<Icon><path d="M3 12h18M3 12l5-5M3 12l5 5" /><path d="M21 12l-5-5M21 12l-5 5" /></Icon>}
          title="Cost of Carry"
          onOpenTab={() => onOpenTab("forward-pricing")}
        >
          <p style={{ marginTop: 0, fontSize: "0.8rem", color: "var(--text-dim)" }}>r fixed at 5%, no income/cost</p>
          <div className="slider-grid">
            <Slider label="Spot (S0)" value={ccS0} min={10} max={300} step={1} onChange={setCcS0} format={num} />
            <Slider label="Maturity (T)" value={ccT} min={0.25} max={5} step={0.25} onChange={setCcT} format={yrs} />
          </div>
          <div className="stat-row">
            <div className="stat stat-hero">
              <span className="label">F0(T)</span>
              <span className="value">{num(ccForward)}</span>
            </div>
          </div>
          <ResponsiveContainer width="100%" height={100}>
            <LineChart data={ccCurve} margin={{ top: 4, right: 6, left: 0, bottom: 0 }}>
              <XAxis dataKey="t" type="number" domain={["dataMin", "dataMax"]} ticks={[0.1, 5]}
                     tickLine={false} axisLine={lightAxisLine} tick={lightAxisTick}
                     tickFormatter={(v: number) => `${v.toFixed(1)}y`} height={16} />
              <YAxis type="number" domain={ccForwardRange} ticks={ccForwardRange} tickLine={false}
                     axisLine={false} tick={lightAxisTick} width={34} tickFormatter={(v: number) => v.toFixed(0)} />
              <Line type="monotone" dataKey="forward" stroke="#e8a33d" strokeWidth={2} dot={false}
                    isAnimationActive animationDuration={250} animationEasing="ease-out" />
              <ReferenceDot x={ccT} y={ccForward} r={4} fill="#e8a33d" stroke="#0a0e14" strokeWidth={1} className="chart-marker" isFront />
            </LineChart>
          </ResponsiveContainer>
        </DashboardWidget>

        <DashboardWidget
          icon={<Icon><circle cx="12" cy="12" r="9" /><path d="M12 7v5l3 3" /></Icon>}
          title="Forward MTM"
          onOpenTab={() => onOpenTab("forward-mtm")}
        >
          <p style={{ marginTop: 0, fontSize: "0.8rem", color: "var(--text-dim)" }}>S0=100, r=5%, T=1 fixed at initiation</p>
          <div className="slider-grid">
            <Slider label="Valuation date (t)" value={mtmT} min={0} max={1} step={0.025} onChange={setMtmT} format={yrs} />
            <Slider label="Current spot (St)" value={mtmSt} min={50} max={150} step={1} onChange={setMtmSt} format={num} />
          </div>
          <div className="stat-row">
            <div className="stat stat-hero">
              <span className="label">Vt(T)</span>
              <span className="value">{num(mtmVt)}</span>
            </div>
          </div>
          <ResponsiveContainer width="100%" height={100}>
            <ComposedChart data={mtmCurve} margin={{ top: 4, right: 6, left: 0, bottom: 0 }}>
              <XAxis dataKey="st" type="number" domain={["dataMin", "dataMax"]} ticks={[50, 150]}
                     tickLine={false} axisLine={lightAxisLine} tick={lightAxisTick}
                     tickFormatter={(v: number) => v.toFixed(0)} height={16} />
              <YAxis type="number" domain={mtmVtRange} ticks={mtmVtRange} tickLine={false} axisLine={false}
                     tick={lightAxisTick} width={30} tickFormatter={(v: number) => v.toFixed(1)} />
              <Area dataKey="pos" stroke="none" fill="rgba(52,211,153,0.25)" isAnimationActive animationDuration={250} animationEasing="ease-out" />
              <Area dataKey="neg" stroke="none" fill="rgba(248,113,113,0.25)" isAnimationActive animationDuration={250} animationEasing="ease-out" />
              <Line type="monotone" dataKey="vt" stroke="#e8a33d" strokeWidth={2} dot={false}
                    isAnimationActive animationDuration={250} animationEasing="ease-out" />
              <ReferenceDot x={mtmSt} y={mtmVt} r={4} fill="#38bdf8" stroke="#0a0e14" strokeWidth={1} className="chart-marker" isFront />
            </ComposedChart>
          </ResponsiveContainer>
        </DashboardWidget>

        <DashboardWidget
          icon={<Icon><circle cx="9" cy="12" r="6" /><circle cx="15" cy="12" r="6" /></Icon>}
          title="FX Forwards"
          onOpenTab={() => onOpenTab("fx-forwards")}
        >
          <p style={{ marginTop: 0, fontSize: "0.8rem", color: "var(--text-dim)" }}>S0=1.1, T=1 fixed</p>
          <div className="slider-grid">
            <Slider label="Foreign rate (rf)" value={fxRf} min={-0.02} max={0.15} step={0.0025} onChange={setFxRf} format={pct} />
            <Slider label="Domestic rate (rd)" value={fxRd} min={-0.02} max={0.15} step={0.0025} onChange={setFxRd} format={pct} />
          </div>
          <div className="stat-row">
            <div className="stat stat-hero">
              <span className="label">F0,f/d(T)</span>
              <span className="value">{num(fxF0)}</span>
            </div>
            <div className="stat">
              <span className="label">vs. spot</span>
              <span className="value">
                {fxIsPremium && <span className="pill good">Premium</span>}
                {fxIsDiscount && <span className="pill bad">Discount</span>}
                {!fxIsPremium && !fxIsDiscount && <span className="pill neutral">Parity</span>}
              </span>
            </div>
          </div>
        </DashboardWidget>

        <DashboardWidget
          icon={<Icon><path d="M4 19V5" /><path d="M4 19h16" /><path d="M8 15l3-4 3 2 4-6" /></Icon>}
          title="Rates & FRAs"
          onOpenTab={() => onOpenTab("rates-fra")}
        >
          <p style={{ marginTop: 0, fontSize: "0.8rem", color: "var(--text-dim)" }}>Fixed demo curve</p>
          <div className="controls">
            <div className="field">
              <label htmlFor="dash-ifr-a">Period A</label>
              <select id="dash-ifr-a" value={ifrA} onChange={(e) => setIfrA(Number(e.target.value))}>
                {DEMO_CURVE.map((row) => <option key={row.period} value={row.period}>{row.period}</option>)}
              </select>
            </div>
            <div className="field">
              <label htmlFor="dash-ifr-b">Period B</label>
              <select id="dash-ifr-b" value={ifrB} onChange={(e) => setIfrB(Number(e.target.value))}>
                {DEMO_CURVE.map((row) => <option key={row.period} value={row.period}>{row.period}</option>)}
              </select>
            </div>
          </div>
          <div className="stat-row">
            <div className="stat stat-hero">
              <span className="label">IFR</span>
              <span className="value">{ifrResult === null ? "Pick B > A" : pct(ifrResult)}</span>
            </div>
          </div>
          <YieldCurveChart
            curve={DEMO_CURVE} height={110} compact
            highlight={ifrResult !== null ? { periodA: ifrA, periodB: ifrB, forwardRate: ifrResult } : null}
          />
        </DashboardWidget>

        <DashboardWidget
          icon={<Icon><path d="M3 17l6-8 4 4 8-9" /><path d="M3 21h18" /></Icon>}
          title="Options Payoff"
          onOpenTab={() => onOpenTab("options-payoff")}
        >
          <p style={{ marginTop: 0, fontSize: "0.8rem", color: "var(--text-dim)" }}>X=100, premium=5 fixed</p>
          <div className="controls">
            <div className="field">
              <label htmlFor="dash-position">Position</label>
              <select id="dash-position" value={optPosition} onChange={(e) => setOptPosition(e.target.value as "long_call" | "long_put")}>
                <option value="long_call">Long call</option>
                <option value="long_put">Long put</option>
              </select>
            </div>
          </div>
          <Slider label="Spot at maturity (ST)" value={optST} min={50} max={150} step={1} onChange={setOptST} format={num} />
          <div className="stat-row">
            <div className="stat stat-hero">
              <span className="label">Profit</span>
              <span className="value">{num(optProfit)}</span>
            </div>
          </div>
          <ResponsiveContainer width="100%" height={100}>
            <ComposedChart data={optCurve} margin={{ top: 4, right: 6, left: 0, bottom: 0 }}>
              <XAxis dataKey="sT" type="number" domain={["dataMin", "dataMax"]} ticks={[50, 150]}
                     tickLine={false} axisLine={lightAxisLine} tick={lightAxisTick}
                     tickFormatter={(v: number) => v.toFixed(0)} height={16} />
              <YAxis type="number" domain={optProfitRange} ticks={optProfitRange} tickLine={false}
                     axisLine={false} tick={lightAxisTick} width={30} tickFormatter={(v: number) => v.toFixed(0)} />
              <Area dataKey="pos" stroke="none" fill="rgba(52,211,153,0.25)" isAnimationActive animationDuration={250} animationEasing="ease-out" />
              <Area dataKey="neg" stroke="none" fill="rgba(248,113,113,0.25)" isAnimationActive animationDuration={250} animationEasing="ease-out" />
              <Line type="monotone" dataKey="profit" stroke="#e8a33d" strokeWidth={2} dot={false}
                    isAnimationActive animationDuration={250} animationEasing="ease-out" />
              <ReferenceDot x={optST} y={optProfit} r={4} fill="#38bdf8" stroke="#0a0e14" strokeWidth={1} className="chart-marker" isFront />
            </ComposedChart>
          </ResponsiveContainer>
        </DashboardWidget>
      </div>
    </section>
  );
}
