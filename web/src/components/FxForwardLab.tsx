import { useState } from "react";
import { Katex } from "./Katex";
import { Slider } from "./Slider";
import {
  fxForwardPrice, fxForwardMtm, LATEX_FX_FORWARD_PRICE, LATEX_FX_FORWARD_MTM,
} from "../lib/financeFormulas";

const pct = (v: number) => `${(v * 100).toFixed(2)}%`;
const num = (v: number) => v.toFixed(4);
const yrs = (v: number) => `${v.toFixed(2)}y`;

// Section 3 (FX forwards). Same covered-interest-rate-parity shape as Section 1's cost
// of carry, but the "carry rate" is the foreign/domestic interest rate differential
// (rf - rd) instead of a single asset's income/cost -- highlighted directly below by
// showing that differential as its own stat.
export function FxForwardLab() {
  const [s0, setS0] = useState(1.1);
  const [rf, setRf] = useState(0.03);
  const [rd, setRd] = useState(0.05);
  const [maturity, setMaturity] = useState(1);
  const [t, setT] = useState(0.5);
  const [st, setSt] = useState(1.12);

  const f0 = fxForwardPrice(s0, rf, rd, maturity);
  const vt = fxForwardMtm(st, f0, rf, rd, maturity, t);

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

        <Katex latex={LATEX_FX_FORWARD_PRICE} />
        <div className="stat-row">
          <div className="stat stat-hero">
            <span className="label">F0,f/d(T)</span>
            <span className="value">{num(f0)}</span>
          </div>
        </div>
      </div>

      <h3>Mark-to-market before expiration</h3>
      <div className="card">
        <div className="slider-grid">
          <Slider label="Valuation date (t)" value={t} min={0} max={maturity} step={maturity / 40} onChange={setT} format={yrs} />
          <Slider label="Current spot St,f/d" value={st} min={s0 * 0.7} max={s0 * 1.3} step={0.01} onChange={setSt} format={num} />
        </div>
        <Katex latex={LATEX_FX_FORWARD_MTM} />
        <div className="stat-row">
          <div className="stat stat-hero">
            <span className="label">Vt(T)</span>
            <span className="value">{num(vt)}</span>
          </div>
        </div>
      </div>
    </section>
  );
}
