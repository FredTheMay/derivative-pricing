// Talks to the mcd demo API via relative paths (/price, /cfa-invariants) -- the frontend
// and the API are served from the same CloudFront distribution via path-based routing
// (infra/lib/mcd-stack.ts), so no CORS or environment-specific base URL is needed once
// deployed. See docs/design/07-aws-demo.md sec.4/sec.5.

export type OptionType = "call" | "put";

export interface McPriceResult {
  price: number;
  standard_error: number;
  ci_95_low: number;
  ci_95_high: number;
  path_count: number;
  seed: number;
  elapsed_seconds: number;
  paths_per_second: number;
}

export interface AnalyticPriceResult {
  price: number;
  elapsed_seconds: number;
  risk_neutral_probability?: number;
  up_factor?: number;
  down_factor?: number;
}

export interface GreeksResult {
  delta: number;
  gamma: number;
  vega: number;
  theta: number;
  rho: number;
  elapsed_seconds: number;
}

export interface ErrorResult {
  error: string;
}

export interface CfaInvariantRow {
  module: string;
  invariant: string;
  pass: boolean;
  deviation: number;
}

async function postJson<T>(path: string, body: Record<string, unknown>): Promise<T> {
  const res = await fetch(path, {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify(body),
  });
  const data = (await res.json()) as T | ErrorResult;
  if (!res.ok || "error" in (data as ErrorResult)) {
    throw new Error((data as ErrorResult).error ?? `request failed with status ${res.status}`);
  }
  return data as T;
}

export function priceEuropean(params: {
  spot: number;
  strike: number;
  rate: number;
  carry_yield: number;
  vol: number;
  time: number;
  type: OptionType;
  path_count: number;
  seed: number;
}): Promise<McPriceResult> {
  return postJson("/price", { product: "european", ...params });
}

export function greeksEuropean(params: {
  spot: number;
  strike: number;
  rate: number;
  carry_yield: number;
  vol: number;
  time: number;
  type: OptionType;
  path_count: number;
  seed: number;
}): Promise<GreeksResult> {
  return postJson("/price", { product: "european", request: "greeks", ...params });
}

export interface LrGreeksResult {
  value: number;
  standard_error: number;
}

export interface LrGreeksResponse {
  delta: number;
  delta_se: number;
  gamma: number;
  gamma_se: number;
  vega: number;
  vega_se: number;
  rho: number;
  rho_se: number;
  theta?: number;
  theta_se?: number;
}

// Likelihood-ratio Greeks (Stretch Goal 1) -- fixes finite-difference gamma's documented
// weak point for discontinuous payoffs. Exposed here for European; see LrVsFdGamma in the
// C++ test suite for the digital/barrier gamma-near-discontinuity comparison this stretch
// goal is actually about.
export function lrGreeksEuropean(params: {
  spot: number;
  strike: number;
  rate: number;
  carry_yield: number;
  vol: number;
  time: number;
  type: OptionType;
  path_count: number;
  seed: number;
}): Promise<LrGreeksResponse> {
  return postJson("/price", { product: "european", request: "lr_greeks", ...params });
}

export interface PathwiseGreeksResponse {
  delta: number;
  delta_se: number;
  vega: number;
  vega_se: number;
  rho: number;
  rho_se: number;
  // No gamma/theta fields at all -- structurally undefined for pathwise, not just
  // unavailable for European. See docs/design/09-pathwise-greeks.md sec.3.
}

// Pathwise-derivative Greeks (Stretch Goal 2) -- cheap and unbiased for smooth payoffs
// (European here), but has no gamma at all and would be silently wrong for a
// discontinuous payoff's delta (see the C++ PathwiseFailureMode test).
export function pathwiseGreeksEuropean(params: {
  spot: number;
  strike: number;
  rate: number;
  carry_yield: number;
  vol: number;
  time: number;
  type: OptionType;
  path_count: number;
  seed: number;
}): Promise<PathwiseGreeksResponse> {
  return postJson("/price", { product: "european", request: "pathwise_greeks", ...params });
}

export interface QmcResult {
  price: number;
  path_count: number;
  elapsed_seconds: number;
  paths_per_second: number;
  note: string;
  // No standard_error/ci fields -- Sobol QMC is deterministic. See
  // docs/design/10-sobol-qmc.md sec.6.
}

// Sobol-QMC with Brownian-bridge path construction (Stretch Goal 3) -- deterministic,
// beats plain Monte Carlo's O(N^-0.5) convergence, capped at 7 monitoring dimensions
// (docs/design/10-sobol-qmc.md sec.2: the from-scratch-verified dimension budget).
export function qmcSobolEuropean(params: {
  spot: number;
  strike: number;
  rate: number;
  carry_yield: number;
  vol: number;
  time: number;
  type: OptionType;
  path_count: number;
}): Promise<QmcResult> {
  return postJson("/price", { product: "european", request: "qmc_sobol", ...params });
}

export const SOBOL_MAX_DIMENSIONS = 7;

export function qmcSobolAsian(params: {
  spot: number;
  strike: number;
  rate: number;
  carry_yield: number;
  vol: number;
  time: number;
  type: OptionType;
  strike_style: "fixed" | "floating";
  monitoring_points: number;
  path_count: number;
}): Promise<QmcResult> {
  return postJson("/price", {
    product: "asian",
    request: "qmc_sobol",
    average_style: "arithmetic",
    ...params,
  });
}

export function priceProduct<T = McPriceResult | AnalyticPriceResult>(
  body: Record<string, unknown>,
): Promise<T> {
  return postJson("/price", body);
}

export async function fetchCfaInvariants(): Promise<CfaInvariantRow[]> {
  const res = await fetch("/cfa-invariants");
  const data = (await res.json()) as { invariants: CfaInvariantRow[] } | ErrorResult;
  if (!res.ok || "error" in data) {
    throw new Error((data as ErrorResult).error ?? `request failed with status ${res.status}`);
  }
  return data.invariants;
}
