import { useState } from "react";
import { priceProduct, type McPriceResult } from "../api";

// Section 3 (docs/design/07-aws-demo.md sec.4): antithetic/control-variate on vs. off,
// live -- two seeded API calls, compared by standard error.
export function VarianceReductionComparison() {
  const [plain, setPlain] = useState<McPriceResult | null>(null);
  const [antithetic, setAntithetic] = useState<McPriceResult | null>(null);
  const [loading, setLoading] = useState(false);
  const [error, setError] = useState<string | null>(null);

  async function run() {
    setLoading(true);
    setError(null);
    try {
      const params = {
        product: "european", spot: 100, strike: 100, rate: 0.05, carry_yield: 0.0,
        vol: 0.2, time: 1.0, type: "call" as const, path_count: 200_000, seed: 7,
      };
      const [p, a] = await Promise.all([
        priceProduct<McPriceResult>({ ...params, antithetic: false }),
        priceProduct<McPriceResult>({ ...params, antithetic: true }),
      ]);
      setPlain(p);
      setAntithetic(a);
    } catch (e) {
      setError(e instanceof Error ? e.message : String(e));
    } finally {
      setLoading(false);
    }
  }

  const factor = plain && antithetic
    ? (plain.standard_error / antithetic.standard_error) ** 2
    : null;

  return (
    <section>
      <h2>Variance Reduction: Antithetic On vs. Off</h2>
      <p>
        Same seed, same path count, antithetic variates on vs. off -- the variance-
        reduction factor is the ratio of the squared standard errors. See Phase 3's factor
        table in docs/validation-report.md for the full measured set across products.
      </p>

      <div className="card">
        <button className="primary" onClick={run} disabled={loading}>
          {loading && <span className="spinner" />}
          {loading ? "Pricing..." : "Compare"}
        </button>
        {error && <p className="error">{error}</p>}

        {plain && antithetic && (
          <div className="table-wrap">
            <table>
              <thead>
                <tr><th></th><th>Price</th><th>Standard Error</th></tr>
              </thead>
              <tbody>
                <tr><td>Plain</td><td className="num">{plain.price.toFixed(4)}</td><td className="num">{plain.standard_error.toFixed(5)}</td></tr>
                <tr><td>Antithetic</td><td className="num">{antithetic.price.toFixed(4)}</td><td className="num">{antithetic.standard_error.toFixed(5)}</td></tr>
              </tbody>
            </table>
          </div>
        )}

        {factor !== null && (
          <div className="stat-row">
            <div className="stat stat-hero">
              <span className="label">Variance-reduction factor</span>
              <span className="value">{factor.toFixed(2)}x</span>
            </div>
          </div>
        )}
      </div>
    </section>
  );
}
