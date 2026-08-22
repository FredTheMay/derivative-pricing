import { useState } from "react";
import {
  Area,
  CartesianGrid,
  ComposedChart,
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

  const tooltipStyle = {
    background: "#161c28", border: "1px solid #212838", borderRadius: 8,
    fontFamily: "'IBM Plex Mono', monospace", fontSize: 12, color: "#f2f4f8",
  };
  const axisStyle = { fill: "#7c8494", fontSize: 11, fontFamily: "'IBM Plex Mono', monospace" };

  return (
    <section>
      <h2>Convergence Explorer</h2>
      <p>
        Live European call price and 95% confidence interval vs. path count (S=K=100,
        r=5%, &sigma;=20%, T=1). Each point is a real, seeded API call -- the band should
        visibly narrow as path count grows and stay centered on the analytic
        Black-Scholes-Merton value ({analytic.toFixed(4)}).
      </p>

      <div className="card">
        <button className="primary" onClick={run} disabled={loading}>
          {loading && <span className="spinner" />}
          {loading ? "Pricing..." : "Run convergence sweep"}
        </button>
        {error && <p className="error">{error}</p>}
        {points.length > 0 && (
          <ResponsiveContainer width="100%" height={320}>
            <ComposedChart data={points} margin={{ top: 10, right: 12, left: 0, bottom: 0 }}>
              <defs>
                <linearGradient id="ciBand" x1="0" y1="0" x2="0" y2="1">
                  <stop offset="0%" stopColor="#e8a33d" stopOpacity={0.28} />
                  <stop offset="100%" stopColor="#e8a33d" stopOpacity={0.06} />
                </linearGradient>
              </defs>
              <CartesianGrid stroke="#212838" strokeDasharray="3 3" />
              <XAxis dataKey="path_count" scale="log" domain={["auto", "auto"]} tick={axisStyle} stroke="#212838" />
              <YAxis domain={["auto", "auto"]} tick={axisStyle} stroke="#212838" />
              <Tooltip contentStyle={tooltipStyle} labelStyle={{ color: "#7c8494" }} />
              <ReferenceLine y={analytic} stroke="#38bdf8" strokeDasharray="4 4" label={{ value: "BSM", fill: "#38bdf8", fontSize: 11 }} />
              <Area type="monotone" dataKey={(d: Point) => [d.ci_low, d.ci_high]}
                    stroke="none" fill="url(#ciBand)" name="95% CI band" isAnimationActive />
              <Line type="monotone" dataKey="price" stroke="#e8a33d" strokeWidth={2.5} dot={{ r: 3, fill: "#e8a33d" }} name="price" />
            </ComposedChart>
          </ResponsiveContainer>
        )}
      </div>

      <h3>Sobol QMC vs. plain Monte Carlo (Stretch Goal 3)</h3>
      <p>
        Sobol quasi-Monte Carlo with Brownian-bridge construction is deterministic --
        it has no standard error -- so its accuracy is measured directly against the
        analytic price instead. Plotted log-log, Sobol's error should fall off faster
        than plain MC's O(N<sup>-0.5</sup>) rate. See{" "}
        <code>docs/design/10-sobol-qmc.md</code> sec.6.
      </p>

      <div className="card">
        <button className="primary" onClick={runQmcComparison} disabled={qmcLoading}>
          {qmcLoading && <span className="spinner" />}
          {qmcLoading ? "Comparing..." : "Compare with Sobol QMC"}
        </button>
        {qmcError && <p className="error">{qmcError}</p>}
        {qmcPoints.length > 0 && (
          <ResponsiveContainer width="100%" height={320}>
            <LineChart data={qmcPoints} margin={{ top: 10, right: 12, left: 0, bottom: 0 }}>
              <CartesianGrid stroke="#212838" strokeDasharray="3 3" />
              <XAxis dataKey="path_count" scale="log" domain={["auto", "auto"]} tick={axisStyle} stroke="#212838" />
              <YAxis scale="log" domain={["auto", "auto"]} tick={axisStyle} stroke="#212838"
                     label={{ value: "|error|", angle: -90, fill: "#7c8494", fontSize: 11 }} />
              <Tooltip contentStyle={tooltipStyle} labelStyle={{ color: "#7c8494" }} />
              <Line type="monotone" dataKey="mc_error" stroke="#f87171" strokeWidth={2} dot={{ r: 3 }} name="plain MC |error|" />
              <Line type="monotone" dataKey="qmc_error" stroke="#34d399" strokeWidth={2} dot={{ r: 3 }} name="Sobol QMC |error|" />
            </LineChart>
          </ResponsiveContainer>
        )}
      </div>
    </section>
  );
}
