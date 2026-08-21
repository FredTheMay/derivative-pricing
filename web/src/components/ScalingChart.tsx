import { useEffect, useState } from "react";
import {
  Bar,
  BarChart,
  CartesianGrid,
  Legend,
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
        <>
          <p>Hardware: {scaling.hardware}. Amdahl serial fraction: {scaling.amdahl_serial_fraction}</p>
          <ResponsiveContainer width="100%" height={280}>
            <BarChart data={scaling.points}>
              <CartesianGrid strokeDasharray="3 3" />
              <XAxis dataKey="threads" label={{ value: "threads", position: "insideBottom", offset: -5 }} />
              <YAxis />
              <Tooltip />
              <Legend />
              <Bar dataKey="speedup" fill="#2166ac" name="speedup vs. 1 thread" />
            </BarChart>
          </ResponsiveContainer>
        </>
      )}
      {falseSharing && (
        <>
          <h3>False-sharing A/B</h3>
          <p>{falseSharing.result}</p>
          <ResponsiveContainer width="100%" height={200}>
            <BarChart data={falseSharing.layouts}>
              <CartesianGrid strokeDasharray="3 3" />
              <XAxis dataKey="layout" />
              <YAxis label={{ value: "median ms", angle: -90, position: "insideLeft" }} />
              <Tooltip />
              <Bar dataKey="median_ms" fill="#b2182b" name="median time (ms)" />
            </BarChart>
          </ResponsiveContainer>
        </>
      )}
    </section>
  );
}
