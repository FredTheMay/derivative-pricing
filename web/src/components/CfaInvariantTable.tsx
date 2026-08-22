import { useEffect, useMemo, useState } from "react";
import { Bar, BarChart, CartesianGrid, Legend, ResponsiveContainer, Tooltip, XAxis, YAxis } from "recharts";
import { fetchCfaInvariants, type CfaInvariantRow } from "../api";

const tooltipStyle = {
  background: "#161c28", border: "1px solid #212838", borderRadius: 8,
  fontFamily: "'IBM Plex Mono', monospace", fontSize: 12, color: "#f2f4f8",
};
const axisStyle = { fill: "#7c8494", fontSize: 11, fontFamily: "'IBM Plex Mono', monospace" };

// Section 6 (docs/design/07-aws-demo.md sec.4): the CFA invariant table, rendered live
// and green -- calls the Lambda's /cfa-invariants route, which runs the same checks
// tests/cfa_invariants_test.cpp makes in C++ (infra/lambda/cfa_invariants.py).
export function CfaInvariantTable() {
  const [rows, setRows] = useState<CfaInvariantRow[] | null>(null);
  const [error, setError] = useState<string | null>(null);
  const [loading, setLoading] = useState(true);

  useEffect(() => {
    fetchCfaInvariants()
      .then(setRows)
      .catch((e) => setError(e instanceof Error ? e.message : String(e)))
      .finally(() => setLoading(false));
  }, []);

  const allPass = rows?.every((r) => r.pass) ?? false;
  const passedCount = rows?.filter((r) => r.pass).length ?? 0;
  const failedCount = rows ? rows.length - passedCount : 0;

  // The flat table hides that invariants are grouped by CFA module -- a stacked bar per
  // module gives that structure a visual before the detail table underneath it.
  const moduleSummary = useMemo(() => {
    if (!rows) return [];
    const byModule = new Map<string, { module: string; pass: number; fail: number }>();
    for (const r of rows) {
      const entry = byModule.get(r.module) ?? { module: r.module, pass: 0, fail: 0 };
      if (r.pass) entry.pass += 1; else entry.fail += 1;
      byModule.set(r.module, entry);
    }
    return [...byModule.values()];
  }, [rows]);

  return (
    <section>
      <h2>CFA Level I Invariants -- Live</h2>
      <p>
        Computed live, this instant, by the deployed engine -- not a static screenshot.
        Every relationship below is one the CFA Level I curriculum teaches, grouped by
        the learning module it comes from.
      </p>

      <div className="card">
        {loading && (
          <p style={{ display: "flex", alignItems: "center", gap: "0.5rem" }}>
            <span className="spinner" style={{ borderTopColor: "var(--accent)", borderColor: "var(--border)" }} />
            Checking invariants...
          </p>
        )}
        {error && <p className="error">{error}</p>}
        {rows && (
          <>
            <span className={`pill ${allPass ? "good" : "bad"}`}>
              {allPass ? "All invariants pass." : "Some invariants failed -- see below."}
            </span>

            <div className="stat-row">
              <div className="stat">
                <span className="label">Total invariants</span>
                <span className="value">{rows.length}</span>
              </div>
              <div className="stat">
                <span className="label">Passed</span>
                <span className="value" style={{ color: "var(--positive)" }}>{passedCount}</span>
              </div>
              <div className="stat">
                <span className="label">Failed</span>
                <span className="value" style={{ color: failedCount > 0 ? "var(--negative)" : undefined }}>{failedCount}</span>
              </div>
            </div>

            <ResponsiveContainer width="100%" height={160}>
              <BarChart data={moduleSummary} margin={{ top: 10, right: 12, left: 0, bottom: 0 }}>
                <CartesianGrid stroke="#212838" strokeDasharray="3 3" />
                <XAxis dataKey="module" tick={axisStyle} stroke="#212838" />
                <YAxis tick={axisStyle} stroke="#212838" allowDecimals={false} />
                <Tooltip contentStyle={tooltipStyle} labelStyle={{ color: "#7c8494" }} />
                <Legend verticalAlign="top" height={24} wrapperStyle={{ fontSize: 11, fontFamily: "'IBM Plex Mono', monospace" }} />
                <Bar dataKey="pass" stackId="a" fill="#34d399" name="pass" isAnimationActive={false} />
                <Bar dataKey="fail" stackId="a" fill="#f87171" name="fail" isAnimationActive={false} />
              </BarChart>
            </ResponsiveContainer>

            <div className="table-wrap" style={{ marginTop: "0.9rem" }}>
              <table>
                <thead>
                  <tr><th>Module</th><th>Invariant</th><th>Result</th><th>Deviation</th></tr>
                </thead>
                <tbody>
                  {rows.map((r, i) => (
                    <tr key={i}>
                      <td>{r.module}</td>
                      <td>{r.invariant}</td>
                      <td className={r.pass ? "status-pass" : "status-fail"}>{r.pass ? "PASS" : "FAIL"}</td>
                      <td className="num">{r.deviation.toExponential(2)}</td>
                    </tr>
                  ))}
                </tbody>
              </table>
            </div>
          </>
        )}
      </div>
    </section>
  );
}
