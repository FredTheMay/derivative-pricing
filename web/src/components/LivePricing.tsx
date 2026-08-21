import { useState } from "react";
import { priceProduct, type AnalyticPriceResult, type McPriceResult, type OptionType } from "../api";

const PRODUCTS = [
  "european", "digital", "asian", "barrier", "lookback", "american",
  "binomial_european", "binomial_american", "forward",
] as const;
type Product = (typeof PRODUCTS)[number];

function buildRequest(product: Product): Record<string, unknown> {
  const type: OptionType = "call";
  const common = { spot: 100, strike: 100, rate: 0.05, carry_yield: 0.0, vol: 0.2, time: 1.0 };
  switch (product) {
    case "forward":
      return { product, spot: 100, rate: 0.05, carry_yield: 0.02, time: 1.0 };
    case "binomial_european":
    case "binomial_american":
      return { product, ...common, type, steps: 500 };
    case "digital":
      return { product, ...common, type, digital_style: "cash_or_nothing", cash_amount: 1.0,
                path_count: 200_000, seed: 42 };
    case "asian":
      return { product, ...common, type, strike_style: "fixed", average_style: "geometric",
                monitoring_points: 12, path_count: 200_000, seed: 42 };
    case "barrier":
      return { product, ...common, type, barrier: 120, direction: "up", knock: "out",
                monitoring_points: 50, path_count: 200_000, seed: 42 };
    case "lookback":
      return { product, ...common, type, style: "floating", monitoring_points: 50,
                path_count: 200_000, seed: 42 };
    case "american":
      return { product, ...common, type, monitoring_points: 20, path_count: 100_000, seed: 42 };
    case "european":
    default:
      return { product, ...common, type, path_count: 200_000, seed: 42 };
  }
}

// Section 4 (docs/design/07-aws-demo.md sec.4): live pricing across all products, always
// showing the 95% CI for Monte Carlo results, never a bare number.
export function LivePricing() {
  const [product, setProduct] = useState<Product>("european");
  const [result, setResult] = useState<McPriceResult | AnalyticPriceResult | null>(null);
  const [loading, setLoading] = useState(false);
  const [error, setError] = useState<string | null>(null);

  async function run() {
    setLoading(true);
    setError(null);
    setResult(null);
    try {
      const r = await priceProduct<McPriceResult | AnalyticPriceResult>(buildRequest(product));
      setResult(r);
    } catch (e) {
      setError(e instanceof Error ? e.message : String(e));
    } finally {
      setLoading(false);
    }
  }

  const hasCi = result !== null && "standard_error" in result;

  return (
    <section>
      <h2>Live Pricing</h2>
      <p>Every Monte Carlo product below always shows its 95% confidence interval, never a bare number.</p>
      <select value={product} onChange={(e) => setProduct(e.target.value as Product)}>
        {PRODUCTS.map((p) => <option key={p} value={p}>{p}</option>)}
      </select>
      <button onClick={run} disabled={loading}>{loading ? "Pricing..." : "Price"}</button>
      {error && <p className="error">{error}</p>}
      {result && (
        <dl>
          <dt>Price</dt><dd>{result.price.toFixed(6)}</dd>
          {hasCi && (
            <>
              <dt>95% CI</dt>
              <dd>
                [{(result as McPriceResult).ci_95_low.toFixed(6)}, {(result as McPriceResult).ci_95_high.toFixed(6)}]
                {" "}(SE={(result as McPriceResult).standard_error.toFixed(6)})
              </dd>
              <dt>Path count</dt><dd>{(result as McPriceResult).path_count.toLocaleString()}</dd>
              <dt>Paths/second</dt><dd>{(result as McPriceResult).paths_per_second.toLocaleString(undefined, { maximumFractionDigits: 0 })}</dd>
            </>
          )}
          {!hasCi && <p><em>Deterministic result -- no sampling error, so no confidence interval is reported.</em></p>}
        </dl>
      )}
    </section>
  );
}
