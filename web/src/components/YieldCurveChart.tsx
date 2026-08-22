import {
  CartesianGrid, Legend, Line, LineChart, ResponsiveContainer, Tooltip, XAxis, YAxis,
} from "recharts";

export interface YieldCurvePoint {
  period: number;
  spotRate: number;
}

interface YieldCurveChartProps {
  curve: YieldCurvePoint[];
  /** When set, overlays the two-point segment an implied forward rate "bridges". */
  highlight?: { periodA: number; periodB: number; forwardRate: number } | null;
  /** Compact mode hides axes/tooltip/legend chrome -- used by the Dashboard's mini widget. */
  compact?: boolean;
  height?: number;
}

const tooltipStyle = {
  background: "#161c28", border: "1px solid #212838", borderRadius: 8,
  fontFamily: "'IBM Plex Mono', monospace", fontSize: 12, color: "#f2f4f8",
};
const axisStyle = { fill: "#7c8494", fontSize: 11, fontFamily: "'IBM Plex Mono', monospace" };
const pct = (v: number) => `${(v * 100).toFixed(2)}%`;

// A very light axis for compact mode -- just the min/max tick, small and dim, so a glance
// gives a sense of scale without turning the mini widget into a full chart.
const lightAxisTick = { fontSize: 9, fill: "#4a5468", fontFamily: "'IBM Plex Mono', monospace" };
const lightAxisLine = { stroke: "#1c2330" };

// Shared by RatesFraLab (the full editable-curve calculator) and Dashboard (a compact
// read-only preview against a fixed demo curve) -- the same component in both places, not
// a duplicated one-off, so the "yield curve" visual language is identical everywhere it
// appears. Dots are shown on the spot-rate line deliberately (unlike every other chart in
// this app, which hides dots) because the curve's control points ARE the data the user is
// directly editing in the table above it, not samples of a smooth underlying function.
export function YieldCurveChart({ curve, highlight = null, compact = false, height = 260 }: YieldCurveChartProps) {
  const sorted = [...curve].sort((a, b) => a.period - b.period);
  const bridge = highlight
    ? [
        { period: highlight.periodA, forwardRate: highlight.forwardRate },
        { period: highlight.periodB, forwardRate: highlight.forwardRate },
      ]
    : null;

  // Explicit min/max ticks, computed from the actual data -- Recharts' automatic "nice
  // tick" generation recurses infinitely for some tiny tickCount/domain combinations on
  // this version, so compact mode's light axis always passes its own literal tick values.
  const periodRange: [number, number] = [sorted[0]?.period ?? 0, sorted[sorted.length - 1]?.period ?? 1];
  const allRates = [...sorted.map((p) => p.spotRate), ...(bridge?.map((p) => p.forwardRate) ?? [])];
  const rateRange: [number, number] = [Math.min(...allRates), Math.max(...allRates)];

  return (
    <ResponsiveContainer width="100%" height={height}>
      <LineChart margin={compact ? { top: 4, right: 4, left: 4, bottom: 4 } : { top: 10, right: 16, left: 0, bottom: 0 }}>
        {!compact && <CartesianGrid stroke="#212838" strokeDasharray="3 3" />}
        {compact ? (
          <XAxis
            dataKey="period" type="number" domain={periodRange} ticks={periodRange} tickLine={false}
            axisLine={lightAxisLine} tick={lightAxisTick}
            tickFormatter={(v: number) => `${v}y`} height={16}
          />
        ) : (
          <XAxis
            dataKey="period" type="number" domain={["auto", "auto"]} tick={axisStyle} stroke="#212838"
            label={{ value: "period (years)", position: "bottom", offset: 0, fill: "#7c8494", fontSize: 11 }}
          />
        )}
        {compact ? (
          <YAxis domain={rateRange} ticks={rateRange} tickLine={false} axisLine={false} tick={lightAxisTick}
                 width={32} tickFormatter={(v: number) => `${(v * 100).toFixed(1)}%`} />
        ) : (
          <YAxis tick={axisStyle} stroke="#212838" tickFormatter={pct} width={54} />
        )}
        {!compact && (
          <Tooltip
            contentStyle={tooltipStyle} labelStyle={{ color: "#7c8494" }}
            formatter={(v: number) => pct(v)} labelFormatter={(l) => `period ${l}`}
          />
        )}
        {!compact && bridge && <Legend verticalAlign="top" height={24} wrapperStyle={{ fontSize: 11, fontFamily: "'IBM Plex Mono', monospace" }} />}
        <Line
          data={sorted} dataKey="spotRate" name="Spot curve" stroke="#e8a33d" strokeWidth={2}
          dot={{ r: compact ? 3 : 4, fill: "#e8a33d", strokeWidth: 0 }}
          isAnimationActive animationDuration={250} animationEasing="ease-out"
        />
        {bridge && (
          <Line
            data={bridge} dataKey="forwardRate" name="Implied forward rate" stroke="#38bdf8"
            strokeWidth={2} strokeDasharray="4 3" dot={{ r: compact ? 3 : 4, fill: "#38bdf8", strokeWidth: 0 }}
            isAnimationActive animationDuration={250} animationEasing="ease-out"
          />
        )}
      </LineChart>
    </ResponsiveContainer>
  );
}
