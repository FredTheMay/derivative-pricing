import { useMemo, useState } from "react";
import { Katex } from "./Katex";
import { Slider } from "./Slider";
import {
  computeDiscountFactors, impliedForwardRate, convertPeriodicity, fraNetPayment,
  irFuturesPrice, contractBpv,
  LATEX_DISCOUNT_FACTOR, LATEX_IFR, LATEX_PERIODICITY, LATEX_FRA_NET_PAYMENT,
  LATEX_IR_FUTURES_PRICE, LATEX_CONTRACT_BPV,
} from "../lib/financeFormulas";

const pct = (v: number) => `${(v * 100).toFixed(3)}%`;
const num = (v: number) => v.toFixed(6);
const money = (v: number) => v.toLocaleString(undefined, { maximumFractionDigits: 0 });

interface CurveRow { id: number; period: number; spotRate: number }

let nextRowId = 1;
function makeRow(period: number, spotRate: number): CurveRow {
  return { id: nextRowId++, period, spotRate };
}

const PERIODICITIES = [1, 2, 4, 12, 365] as const;

// Section 4 (rates, FRAs, futures) -- the most complex section, per docs/design/13-cfa-
// education-dashboard.md: one scrollable panel, <h3>-divided cards, matching the
// existing ConvergenceExplorer/ScalingChart multi-topic-in-one-component pattern rather
// than sub-tabs or an accordion (which would hide the yield-curve editor from the
// calculators that depend on it). Only the IFR calculator depends on the shared curve
// state; periodicity/FRA/futures each carry their own local, independent state.
export function RatesFraLab() {
  const [curve, setCurve] = useState<CurveRow[]>([
    makeRow(0.5, 0.02), makeRow(1, 0.025), makeRow(1.5, 0.028), makeRow(2, 0.03),
  ]);
  const curveWithDf = useMemo(() => computeDiscountFactors(curve), [curve]);

  const [ifrPeriodA, setIfrPeriodA] = useState(0.5);
  const [ifrPeriodB, setIfrPeriodB] = useState(1);

  const [aprM, setAprM] = useState(0.06);
  const [m, setM] = useState<number>(2);
  const [n, setN] = useState<number>(12);

  const [mrr, setMrr] = useState(0.05);
  const [ifr, setIfr] = useState(0.045);
  const [notional, setNotional] = useState(1_000_000);
  const [period, setPeriod] = useState(0.25);

  const [futuresMrr, setFuturesMrr] = useState(0.0325);
  const [futuresNotional, setFuturesNotional] = useState(1_000_000);
  const [futuresPeriod, setFuturesPeriod] = useState(0.25);

  function updateRow(id: number, field: "period" | "spotRate", value: number) {
    setCurve((rows) => rows.map((row) => (row.id === id ? { ...row, [field]: value } : row)));
  }
  function addRow() {
    const last = curve[curve.length - 1];
    setCurve((rows) => [...rows, makeRow((last?.period ?? 0) + 0.5, last?.spotRate ?? 0.03)]);
  }
  function removeRow(id: number) {
    setCurve((rows) => (rows.length > 1 ? rows.filter((row) => row.id !== id) : rows));
  }

  const rowA = curve.find((r) => r.period === ifrPeriodA);
  const rowB = curve.find((r) => r.period === ifrPeriodB);
  const ifrResult = rowA && rowB && rowB.period > rowA.period
    ? impliedForwardRate(rowA.spotRate, rowA.period, rowB.spotRate, rowB.period)
    : null;

  const aprN = convertPeriodicity(aprM, m, n);
  const netPayment = fraNetPayment(mrr, ifr, notional, period);
  const futuresPrice = irFuturesPrice(futuresMrr);
  const bpv = contractBpv(futuresNotional, futuresPeriod);

  return (
    <section>
      <h2>Interest Rates, FRAs &amp; Futures</h2>
      <p>
        Build a spot-rate curve, derive discount factors and implied forward rates from
        it, then use the standalone calculators below for periodicity conversion, FRA
        settlement, and interest rate futures.
      </p>

      <h3>Spot-rate curve &amp; discount factors</h3>
      <div className="card">
        <Katex latex={LATEX_DISCOUNT_FACTOR} />
        <div className="table-wrap">
          <table>
            <thead>
              <tr><th>Period</th><th>Spot rate z<sub>i</sub></th><th>Discount factor</th><th></th></tr>
            </thead>
            <tbody>
              {curveWithDf.map((row) => (
                <tr key={row.id}>
                  <td>
                    <input className="curve-row-input" type="number" step={0.25} value={row.period}
                           onChange={(e) => updateRow(row.id, "period", Number(e.target.value))} />
                  </td>
                  <td>
                    <input className="curve-row-input" type="number" step={0.001} value={row.spotRate}
                           onChange={(e) => updateRow(row.id, "spotRate", Number(e.target.value))} />
                  </td>
                  <td className="num">{num(row.discountFactor)}</td>
                  <td><button className="ghost" onClick={() => removeRow(row.id)}>Remove</button></td>
                </tr>
              ))}
            </tbody>
          </table>
        </div>
        <button className="ghost" style={{ marginTop: "0.75rem" }} onClick={addRow}>+ Add period</button>
      </div>

      <h3>Implied forward rate</h3>
      <div className="card">
        <Katex latex={LATEX_IFR} />
        <div className="controls">
          <div className="field">
            <label htmlFor="ifr-a">Period A</label>
            <select id="ifr-a" value={ifrPeriodA} onChange={(e) => setIfrPeriodA(Number(e.target.value))}>
              {curve.map((row) => <option key={row.id} value={row.period}>{row.period}</option>)}
            </select>
          </div>
          <div className="field">
            <label htmlFor="ifr-b">Period B</label>
            <select id="ifr-b" value={ifrPeriodB} onChange={(e) => setIfrPeriodB(Number(e.target.value))}>
              {curve.map((row) => <option key={row.id} value={row.period}>{row.period}</option>)}
            </select>
          </div>
        </div>
        <div className="stat-row">
          <div className="stat stat-hero">
            <span className="label">IFR<sub>A,B-A</sub></span>
            <span className="value">{ifrResult === null ? "Pick B > A" : pct(ifrResult)}</span>
          </div>
        </div>
      </div>

      <h3>Periodicity conversion</h3>
      <div className="card">
        <Katex latex={LATEX_PERIODICITY} />
        <div className="controls">
          <div className="field">
            <label htmlFor="apr-m">Compounding freq. m</label>
            <select id="apr-m" value={m} onChange={(e) => setM(Number(e.target.value))}>
              {PERIODICITIES.map((p) => <option key={p} value={p}>{p}x / year</option>)}
            </select>
          </div>
          <div className="field">
            <label htmlFor="apr-n">Target freq. n</label>
            <select id="apr-n" value={n} onChange={(e) => setN(Number(e.target.value))}>
              {PERIODICITIES.map((p) => <option key={p} value={p}>{p}x / year</option>)}
            </select>
          </div>
        </div>
        <Slider label={`APR compounded ${m}x/year`} value={aprM} min={0} max={0.2} step={0.0025} onChange={setAprM} format={pct} />
        <div className="stat-row">
          <div className="stat stat-hero">
            <span className="label">Equivalent APR compounded {n}x/year</span>
            <span className="value">{pct(aprN)}</span>
          </div>
        </div>
      </div>

      <h3>FRA net payment</h3>
      <div className="card">
        <Katex latex={LATEX_FRA_NET_PAYMENT} />
        <div className="slider-grid">
          <Slider label="MRR at settlement" value={mrr} min={0} max={0.15} step={0.001} onChange={setMrr} format={pct} />
          <Slider label="Contract IFR" value={ifr} min={0} max={0.15} step={0.001} onChange={setIfr} format={pct} />
          <Slider label="Notional principal" value={notional} min={100_000} max={10_000_000} step={100_000} onChange={setNotional} format={money} />
          <Slider label="Period (years)" value={period} min={0.0833} max={1} step={0.0833} onChange={setPeriod} format={(v) => v.toFixed(4)} />
        </div>
        <div className="stat-row">
          <div className="stat stat-hero">
            <span className="label">Net payment</span>
            <span className="value">{netPayment.toFixed(2)}</span>
          </div>
        </div>
      </div>

      <h3>Interest rate futures</h3>
      <div className="card">
        <Katex latex={LATEX_IR_FUTURES_PRICE} />
        <Slider label="MRR" value={futuresMrr} min={0} max={0.15} step={0.0005} onChange={setFuturesMrr} format={pct} />
        <div className="stat-row">
          <div className="stat">
            <span className="label">Futures price</span>
            <span className="value">{futuresPrice.toFixed(4)}</span>
          </div>
        </div>

        <Katex latex={LATEX_CONTRACT_BPV} />
        <div className="slider-grid">
          <Slider label="Notional principal" value={futuresNotional} min={100_000} max={10_000_000} step={100_000} onChange={setFuturesNotional} format={money} />
          <Slider label="Period (years)" value={futuresPeriod} min={0.0833} max={1} step={0.0833} onChange={setFuturesPeriod} format={(v) => v.toFixed(4)} />
        </div>
        <div className="stat-row">
          <div className="stat stat-hero">
            <span className="label">Contract BPV</span>
            <span className="value">{bpv.toFixed(2)}</span>
          </div>
        </div>
      </div>
    </section>
  );
}
