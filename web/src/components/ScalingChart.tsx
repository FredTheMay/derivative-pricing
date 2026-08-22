import { useEffect, useMemo, useState } from "react";
import {
  Bar,
  BarChart,
  CartesianGrid,
  ComposedChart,
  Legend,
  Line,
  ResponsiveContainer,
  Tooltip,
  XAxis,
  YAxis,
} from "recharts";

interface ScalingPoint {
  threads: number;
  speedup: number;
  efficiency: number;
}

interface ScalingData {
  hardware: string;
  amdahl_serial_fraction: number;
  points: ScalingPoint[];
}

interface FalseSharingData {
  result: string;
  layouts: { layout: string; median_ms: number }[];
}

// Section 2 (docs/design/07-aws-demo.md sec.4): thread-scaling and false-sharing A/B,
// served from precomputed committed JSON, never computed on request -- a Lambda
// invocation cannot reproduce a controlled multi-core scaling measurement.
export function ScalingChart() {
  const [scaling, setScaling] = useState<ScalingData | null>(null);
  const [falseSharing, setFalseSharing] = useState<FalseSharingData | null>(null);
  const [error, setError] = useState<string | null>(null);

  useEffect(() => {
    Promise.all([
      fetch("/benchmarks/scaling.json").then((r) => r.json()),
      fetch("/benchmarks/false_sharing.json").then((r) => r.json()),
    ])
      .then(([s, f]) => {
        setScaling(s);
        setFalseSharing(f);
      })
      .catch((e) => setError(String(e)));
  }, []);

  // Amdahl's law's ideal speedup at each thread count, computed from the already-fetched
  // serial fraction -- makes the gap between measured and theoretical-max scaling visible
  // for the first time, instead of leaving the serial fraction as a bare stat number.
  const pointsWithIdeal = useMemo(() => {
    if (!scaling) return [];
    const f = scaling.amdahl_serial_fraction;
    return scaling.points.map((p) => ({
      ...p,
      idealSpeedup: 1 / (f + (1 - f) / p.threads),
    }));
  }, [scaling]);

  const tooltipStyle = {
    background: "#161c28", border: "1px solid #212838", borderRadius: 8,
    fontFamily: "'IBM Plex Mono', monospace", fontSize: 12, color: "#f2f4f8",
  };
  const axisStyle = { fill: "#7c8494", fontSize: 11, fontFamily: "'IBM Plex Mono', monospace" };

  return (
    <section>
      <h2>Thread Scaling &amp; False-Sharing A/B</h2>
      <p>
        Precomputed, committed measurements from Phase 4 (docs/validation-report.md) --
        never recomputed on request, since Lambda's shared vCPU allocation bears no
        relationship to the dedicated-machine measurement this chart is about.
      </p>
      {error && <p className="error">{error}</p>}

      {scaling && (
        <div className="card">
          <div className="stat-row" style={{ marginTop: 0, marginBottom: "1rem" }}>
            <div className="stat">
              <span className="label">Hardware</span>
              <span className="value" style={{ fontSize: "0.95rem" }}>{scaling.hardware}</span>
            </div>
            <div className="stat">
              <span className="label">Amdahl serial fraction</span>
              <span className="value">{scaling.amdahl_serial_fraction}</span>
            </div>
          </div>
          <ResponsiveContainer width="100%" height={300}>
            <ComposedChart data={pointsWithIdeal} margin={{ top: 8, right: 8, left: 0, bottom: 20 }}>
              <CartesianGrid stroke="#212838" strokeDasharray="3 3" />
              <XAxis dataKey="threads" tick={axisStyle} stroke="#212838"
                     label={{ value: "threads", position: "bottom", offset: 30, fill: "#7c8494", fontSize: 11 }} />
              <YAxis tick={axisStyle} stroke="#212838" />
              <Tooltip contentStyle={tooltipStyle} cursor={{ fill: "rgba(232,163,61,0.06)" }} />
              <Legend verticalAlign="top" height={32} wrapperStyle={{ fontSize: 12, color: "#7c8494" }} />
              <Bar dataKey="speedup" fill="#e8a33d" radius={[4, 4, 0, 0]} name="speedup vs. 1 thread" />
              <Line type="monotone" dataKey="idealSpeedup" stroke="#38bdf8" strokeWidth={2} strokeDasharray="4 4" dot={{ r: 3 }} name="Amdahl ideal" isAnimationActive={false} />
            </ComposedChart>
          </ResponsiveContainer>
        </div>
      )}

      {falseSharing && (
        <>
          <h3>False-sharing A/B</h3>
          <p>{falseSharing.result}</p>
          <div className="card">
            <ResponsiveContainer width="100%" height={200}>
              <BarChart data={falseSharing.layouts}>
                <CartesianGrid stroke="#212838" strokeDasharray="3 3" />
                <XAxis dataKey="layout" tick={axisStyle} stroke="#212838" />
                <YAxis tick={axisStyle} stroke="#212838"
                       label={{ value: "median ms", angle: -90, position: "insideLeft", fill: "#7c8494", fontSize: 11 }} />
                <Tooltip contentStyle={tooltipStyle} cursor={{ fill: "rgba(248,113,113,0.06)" }} />
                <Bar dataKey="median_ms" fill="#f87171" radius={[4, 4, 0, 0]} name="median time (ms)" />
              </BarChart>
            </ResponsiveContainer>
          </div>
        </>
      )}
    </section>
  );
}
