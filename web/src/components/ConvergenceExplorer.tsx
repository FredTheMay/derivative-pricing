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
import { priceEuropean, qmcSobolEuropean, type McPriceResult } from "../api";

const PATH_COUNTS = [1_000, 10_000, 100_000, 1_000_000, 5_000_000];

interface Point {
  path_count: number;
  price: number;
  ci_low: number;
  ci_high: number;
}

interface ErrorPoint {
  path_count: number;
  mc_error: number;
  qmc_error: number;
}

// Section 1 (docs/design/07-aws-demo.md sec.4): price and CI band vs. path count, live --
// each point is a real API call, not a precomputed table.
export function ConvergenceExplorer() {
  const [points, setPoints] = useState<Point[]>([]);
  const [loading, setLoading] = useState(false);
  const [error, setError] = useState<string | null>(null);
  const [qmcPoints, setQmcPoints] = useState<ErrorPoint[]>([]);
  const [qmcLoading, setQmcLoading] = useState(false);
  const [qmcError, setQmcError] = useState<string | null>(null);

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

  async function runQmcComparison() {
    setQmcLoading(true);
    setQmcError(null);
    setQmcPoints([]);
    try {
      const results: ErrorPoint[] = [];
      for (const path_count of PATH_COUNTS) {
        const [mc, qmc] = await Promise.all([
          priceEuropean({
            spot: 100, strike: 100, rate: 0.05, carry_yield: 0.0, vol: 0.2, time: 1.0,
            type: "call", path_count, seed: 42,
          }),
          qmcSobolEuropean({
            spot: 100, strike: 100, rate: 0.05, carry_yield: 0.0, vol: 0.2, time: 1.0,
            type: "call", path_count,
          }),
        ]);
        results.push({
          path_count,
          mc_error: Math.abs(mc.price - analytic),
          qmc_error: Math.abs(qmc.price - analytic),
        });
        setQmcPoints([...results]);
      }
    } catch (e) {
      setQmcError(e instanceof Error ? e.message : String(e));
    } finally {
      setQmcLoading(false);
    }
  }

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

      <h3>Sobol QMC vs. plain Monte Carlo (Stretch Goal 3)</h3>
      <p>
        Sobol quasi-Monte Carlo with Brownian-bridge construction is deterministic --
        it has no standard error -- so its accuracy is measured directly against the
        analytic price instead. Plotted log-log, Sobol's error should fall off faster
        than plain MC's O(N<sup>-0.5</sup>) rate. See{" "}
        <code>docs/design/10-sobol-qmc.md</code> sec.6.
      </p>
      <button onClick={runQmcComparison} disabled={qmcLoading}>
        {qmcLoading ? "Comparing..." : "Compare with Sobol QMC"}
      </button>
      {qmcError && <p className="error">{qmcError}</p>}
      {qmcPoints.length > 0 && (
        <ResponsiveContainer width="100%" height={320}>
          <LineChart data={qmcPoints}>
            <CartesianGrid strokeDasharray="3 3" />
            <XAxis dataKey="path_count" scale="log" domain={["auto", "auto"]} />
            <YAxis scale="log" domain={["auto", "auto"]} label={{ value: "|error|", angle: -90 }} />
            <Tooltip />
            <Line type="monotone" dataKey="mc_error" stroke="#2166ac" name="plain MC |error|" />
            <Line type="monotone" dataKey="qmc_error" stroke="#1a9850" name="Sobol QMC |error|" />
          </LineChart>
        </ResponsiveContainer>
      )}
    </section>
  );
}
