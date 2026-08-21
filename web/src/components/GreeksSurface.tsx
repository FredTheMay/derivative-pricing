import { useState } from "react";
import { greeksEuropean, lrGreeksEuropean } from "../api";

const SPOTS = [80, 90, 100, 110, 120];
const TIMES = [0.25, 0.5, 1.0, 1.5, 2.0];
type Greek = "delta" | "gamma" | "vega" | "theta" | "rho";
type Method = "fd" | "lr";

// Section 5 (docs/design/07-aws-demo.md sec.4): Greeks surfaces over a spot/time grid.
// Method toggle added for Stretch Goal 1 (docs/design/08-likelihood-ratio-greeks.md):
// finite-difference vs. likelihood-ratio, side by side on the same grid -- the frontend
// demonstration of what tests/likelihood_ratio_test.cpp's LrVsFdGamma tests measure.
export function GreeksSurface() {
  const [greek, setGreek] = useState<Greek>("gamma");
  const [method, setMethod] = useState<Method>("lr");
  const [grid, setGrid] = useState<number[][] | null>(null);
  const [loading, setLoading] = useState(false);
  const [error, setError] = useState<string | null>(null);

  async function run() {
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

  function cellColor(v: number): string {
    const t = max === min ? 0.5 : (v - min) / (max - min);
    const r = Math.round(33 + t * (178 - 33));
    const g = Math.round(102 + t * (24 - 102));
    const b = Math.round(172 + t * (43 - 172));
    return `rgb(${r},${g},${b})`;
  }

  return (
    <section>
      <h2>Greeks Surface</h2>
      <p>
        European Greeks over a spot/time grid (K=100, r=5%, &sigma;=20%, N=100,000 paths
        per cell). Switch method to compare finite-difference against likelihood-ratio --
        for gamma especially, LR's standard error is dramatically lower near a
        discontinuity (see the CFA/validation report for the measured digital/barrier
        comparison; this surface uses the smooth European payoff, where both methods
        agree).
      </p>
      <select value={greek} onChange={(e) => setGreek(e.target.value as Greek)}>
        {(["delta", "gamma", "vega", "theta", "rho"] as const).map((g) => (
          <option key={g} value={g}>{g}</option>
        ))}
      </select>
      <select value={method} onChange={(e) => setMethod(e.target.value as Method)}>
        <option value="lr">Likelihood-ratio</option>
        <option value="fd">Finite-difference</option>
      </select>
      <button onClick={run} disabled={loading}>{loading ? "Computing..." : "Compute surface"}</button>
      {error && <p className="error">{error}</p>}
      {grid && (
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
      )}
    </section>
  );
}
