import { useState } from "react";
import {
  CartesianGrid,
  Line,
  LineChart,
  ReferenceLine,
  ResponsiveContainer,
  Tooltip,
  XAxis,
  YAxis,
} from "recharts";
import { priceEuropean, type McPriceResult } from "../api";

const PATH_COUNTS = [1_000, 10_000, 100_000, 1_000_000, 5_000_000];

interface Point {
  path_count: number;
  price: number;
  ci_low: number;
  ci_high: number;
}

// Section 1 (docs/design/07-aws-demo.md sec.4): price and CI band vs. path count, live --
// each point is a real API call, not a precomputed table.
export function ConvergenceExplorer() {
  const [points, setPoints] = useState<Point[]>([]);
  const [loading, setLoading] = useState(false);
  const [error, setError] = useState<string | null>(null);

  async function run() {
    setLoading(true);
    setError(null);
    setPoints([]);
    try {
      const results: Point[] = [];
      for (const path_count of PATH_COUNTS) {
        const r: McPriceResult = await priceEuropean({
          spot: 100,
          strike: 100,
          rate: 0.05,
          carry_yield: 0.0,
          vol: 0.2,
          time: 1.0,
          type: "call",
          path_count,
          seed: 42,
        });
        results.push({
          path_count,
          price: r.price,
          ci_low: r.ci_95_low,
          ci_high: r.ci_95_high,
        });
        setPoints([...results]);
      }
    } catch (e) {
      setError(e instanceof Error ? e.message : String(e));
    } finally {
      setLoading(false);
    }
  }

  const analytic = 10.4506; // BSM value for S=K=100, r=5%, q=0, sigma=20%, T=1 (European call)

  return (
    <section>
      <h2>Convergence Explorer</h2>
      <p>
        Live European call price and 95% confidence interval vs. path count (S=K=100,
        r=5%, &sigma;=20%, T=1). Each point is a real, seeded API call -- the band should
        visibly narrow as path count grows and stay centered on the analytic
        Black-Scholes-Merton value ({analytic.toFixed(4)}).
      </p>
      <button onClick={run} disabled={loading}>
        {loading ? "Pricing..." : "Run convergence sweep"}
      </button>
      {error && <p className="error">{error}</p>}
      {points.length > 0 && (
        <ResponsiveContainer width="100%" height={320}>
          <LineChart data={points}>
            <CartesianGrid strokeDasharray="3 3" />
            <XAxis dataKey="path_count" scale="log" domain={["auto", "auto"]} />
            <YAxis domain={["auto", "auto"]} />
            <Tooltip />
            <ReferenceLine y={analytic} stroke="#888" strokeDasharray="4 4" label="BSM" />
            <Line type="monotone" dataKey="price" stroke="#2166ac" name="price" />
            <Line type="monotone" dataKey="ci_low" stroke="#b2182b" strokeDasharray="2 2"
                  name="95% CI low" dot={false} />
            <Line type="monotone" dataKey="ci_high" stroke="#b2182b" strokeDasharray="2 2"
                  name="95% CI high" dot={false} />
          </LineChart>
        </ResponsiveContainer>
      )}
    </section>
  );
}
