import { useState } from "react";
import { greeksEuropean, lrGreeksEuropean, pathwiseGreeksEuropean } from "../api";

const SPOTS = [80, 90, 100, 110, 120];
const TIMES = [0.25, 0.5, 1.0, 1.5, 2.0];
type Greek = "delta" | "gamma" | "vega" | "theta" | "rho";
type Method = "fd" | "lr" | "pathwise";
const PATHWISE_UNSUPPORTED: ReadonlySet<Greek> = new Set(["gamma", "theta"]);

// Section 5 (docs/design/07-aws-demo.md sec.4): Greeks surfaces over a spot/time grid.
// Method toggle added for Stretch Goal 1 (docs/design/08-likelihood-ratio-greeks.md) and
// Stretch Goal 2 (docs/design/09-pathwise-greeks.md): finite-difference vs. likelihood-
// ratio vs. pathwise, side by side on the same grid. Pathwise has no gamma/theta at all
// (structurally undefined, not just unavailable here) -- the UI disables those
// combinations rather than silently requesting something the API will reject.
export function GreeksSurface() {
  const [greek, setGreek] = useState<Greek>("gamma");
  const [method, setMethod] = useState<Method>("lr");
  const [grid, setGrid] = useState<number[][] | null>(null);
  const [loading, setLoading] = useState(false);
  const [error, setError] = useState<string | null>(null);

  const pathwiseDisabled = method === "pathwise" && PATHWISE_UNSUPPORTED.has(greek);

  async function run() {
    if (pathwiseDisabled) {
      return;
    }
    setLoading(true);
    setError(null);
    setGrid(null);
    try {
      const rows: number[][] = [];
      for (const spot of SPOTS) {
        const row: number[] = [];
        for (const time of TIMES) {
          const params = {
            spot, strike: 100, rate: 0.05, carry_yield: 0.0, vol: 0.2, time,
            type: "call" as const, path_count: 100_000, seed: 42,
          };
          if (method === "lr") {
            const g = await lrGreeksEuropean(params);
            row.push(greek === "theta" ? (g.theta ?? NaN) : g[greek]);
          } else if (method === "pathwise") {
            const g = await pathwiseGreeksEuropean(params);
            row.push(g[greek as "delta" | "vega" | "rho"]);
          } else {
            const g = await greeksEuropean(params);
            row.push(g[greek]);
          }
        }
        rows.push(row);
      }
      setGrid(rows);
    } catch (e) {
      setError(e instanceof Error ? e.message : String(e));
    } finally {
      setLoading(false);
    }
  }

  const flat = grid ? grid.flat() : [];
  const min = flat.length ? Math.min(...flat) : 0;
  const max = flat.length ? Math.max(...flat) : 1;

  // Cool (loss-side / low) -> warm accent (gain-side / high), matching the app's
  // negative/accent palette rather than an arbitrary blue-red gradient.
  function cellColor(v: number): string {
    const t = max === min ? 0.5 : (v - min) / (max - min);
    const lo = { r: 56, g: 78, b: 110 };   // cool slate-blue, low values
    const hi = { r: 232, g: 163, b: 61 };  // accent amber, high values
    const r = Math.round(lo.r + t * (hi.r - lo.r));
    const g = Math.round(lo.g + t * (hi.g - lo.g));
    const b = Math.round(lo.b + t * (hi.b - lo.b));
    return `rgb(${r},${g},${b})`;
  }

  return (
    <section>
      <h2>Greeks Surface</h2>
      <p>
        European Greeks over a spot/time grid (K=100, r=5%, &sigma;=20%, N=100,000 paths
        per cell). Three methods: finite-difference, likelihood-ratio (LR's standard
        error is dramatically lower than FD's for gamma near a discontinuity -- see the
        validation report for the measured digital/barrier comparison), and pathwise
        (cheap and precise for delta/vega/rho, but has no gamma at all -- pathwise
        differentiates the payoff itself, and a payoff's second derivative at a kink is a
        Dirac delta, not a number).
      </p>

      <div className="card">
        <div className="controls">
          <div className="field">
            <label htmlFor="greek-select">Greek</label>
            <select id="greek-select" value={greek} onChange={(e) => setGreek(e.target.value as Greek)}>
              {(["delta", "gamma", "vega", "theta", "rho"] as const).map((g) => (
                <option key={g} value={g}>{g}</option>
              ))}
            </select>
          </div>
          <div className="field">
            <label htmlFor="method-select">Method</label>
            <select id="method-select" value={method} onChange={(e) => setMethod(e.target.value as Method)}>
              <option value="lr">Likelihood-ratio</option>
              <option value="fd">Finite-difference</option>
              <option value="pathwise">Pathwise</option>
            </select>
          </div>
          <button className="primary" onClick={run} disabled={loading || pathwiseDisabled} style={{ alignSelf: "flex-end" }}>
            {loading && <span className="spinner" />}
            {loading ? "Computing..." : "Compute surface"}
          </button>
        </div>

        {pathwiseDisabled && (
          <p className="error">
            Pathwise has no {greek} -- structurally undefined for this method, not just
            unavailable here. Pick delta, vega, or rho, or switch method.
          </p>
        )}
        {error && <p className="error">{error}</p>}

        {grid && (
          <>
            <div className="table-wrap">
              <table className="surface">
                <thead>
                  <tr>
                    <th>S \ T</th>
                    {TIMES.map((t) => <th key={t}>{t}</th>)}
                  </tr>
                </thead>
                <tbody>
                  {grid.map((row, i) => (
                    <tr key={SPOTS[i]}>
                      <th>{SPOTS[i]}</th>
                      {row.map((v, j) => (
                        <td key={j} style={{ backgroundColor: cellColor(v) }}>{v.toFixed(3)}</td>
                      ))}
                    </tr>
                  ))}
                </tbody>
              </table>
            </div>
            <div className="legend">
              <span>min {min.toFixed(3)}</span>
              <span className="bar" style={{ background: `linear-gradient(90deg, rgb(56,78,110), rgb(232,163,61))` }} />
              <span>max {max.toFixed(3)}</span>
            </div>
          </>
        )}
      </div>
    </section>
  );
}
