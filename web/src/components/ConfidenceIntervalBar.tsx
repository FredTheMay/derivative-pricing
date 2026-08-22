interface ConfidenceIntervalBarProps {
  low: number;
  high: number;
  value: number;
  format?: (n: number) => string;
}

// A small glanceable band+marker visual for a Monte Carlo result's 95% CI -- this project's
// own design principle is "never show a bare price without its confidence interval" (see
// CLAUDE.md Phase 6), which today is honored with three flat stat tiles (Price / 95% CI /
// Standard error). This turns those three numbers into one picture: a shaded interval with
// a marker at the point estimate, so the reader sees the uncertainty rather than reading it.
export function ConfidenceIntervalBar({ low, high, value, format }: ConfidenceIntervalBarProps) {
  const fmt = format ?? ((n: number) => n.toFixed(4));
  const center = (low + high) / 2;
  const halfWidth = Math.max((high - low) / 2, Math.abs(value) * 0.02, 1e-9);
  const domainLow = center - halfWidth * 3;
  const domainHigh = center + halfWidth * 3;
  const span = domainHigh - domainLow;
  const pct = (v: number) => `${(((v - domainLow) / span) * 100).toFixed(2)}%`;

  return (
    <div className="ci-bar">
      <div className="ci-bar-value" style={{ left: pct(value) }}>{fmt(value)}</div>
      <div className="ci-bar-track">
        <div className="ci-bar-band" style={{ left: pct(low), width: `${(((high - low) / span) * 100).toFixed(2)}%` }} />
        <div className="ci-bar-marker" style={{ left: pct(value) }} title={`${fmt(value)} [${fmt(low)}, ${fmt(high)}]`} />
      </div>
      <div className="ci-bar-ticks">
        <span>{fmt(low)}</span>
        <span>{fmt(high)}</span>
      </div>
    </div>
  );
}
