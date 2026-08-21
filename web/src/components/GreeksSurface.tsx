import { useState } from "react";
import { greeksEuropean } from "../api";

const SPOTS = [80, 90, 100, 110, 120];
const TIMES = [0.25, 0.5, 1.0, 1.5, 2.0];

// Section 5 (docs/design/07-aws-demo.md sec.4): Greeks surfaces over a spot/time grid --
// a small grid of finite_difference_european calls, rendered as a heatmap.
export function GreeksSurface() {
  const [greek, setGreek] = useState<"delta" | "gamma" | "vega" | "theta" | "rho">("delta");
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
          const g = await greeksEuropean({
            spot, strike: 100, rate: 0.05, carry_yield: 0.0, vol: 0.2, time,
            type: "call", path_count: 100_000, seed: 42,
          });
          row.push(g[greek]);
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
      <p>Finite-difference European Greeks over a spot/time grid (K=100, r=5%, &sigma;=20%, N=100,000 paths per cell).</p>
      <select value={greek} onChange={(e) => setGreek(e.target.value as typeof greek)}>
        {(["delta", "gamma", "vega", "theta", "rho"] as const).map((g) => (
          <option key={g} value={g}>{g}</option>
        ))}
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
