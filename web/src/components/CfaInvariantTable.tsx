import { useEffect, useState } from "react";
import { fetchCfaInvariants, type CfaInvariantRow } from "../api";

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

  return (
    <section>
      <h2>CFA Level I Invariants -- Live</h2>
      <p>
        Computed live, this instant, by the deployed engine -- not a static screenshot.
        See docs/cfa-mapping.md for the module-by-module rationale.
      </p>
      {loading && <p>Checking invariants...</p>}
      {error && <p className="error">{error}</p>}
      {rows && (
        <>
          <p className={allPass ? "status-pass" : "status-fail"}>
            {allPass ? "All invariants pass." : "Some invariants failed -- see below."}
          </p>
          <table>
            <thead>
              <tr><th>Module</th><th>Invariant</th><th>Result</th><th>Deviation</th></tr>
            </thead>
            <tbody>
              {rows.map((r, i) => (
                <tr key={i} className={r.pass ? "status-pass" : "status-fail"}>
                  <td>{r.module}</td>
                  <td>{r.invariant}</td>
                  <td>{r.pass ? "PASS" : "FAIL"}</td>
                  <td>{r.deviation.toExponential(2)}</td>
                </tr>
              ))}
            </tbody>
          </table>
        </>
      )}
    </section>
  );
}
