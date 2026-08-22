# Visual/UX redesign — Dashboard + formula cards + deeper Explore visuals

Status: **implemented**

## 1. Purpose

The first version of the CFA education dashboard (`docs/design/13-cfa-education-
dashboard.md`) worked but read as "a list of equations": bare `<Katex>` blocks
stacked above stat grids, three of the five Learn sections had no
visualization at all, and the original six Explore tabs (Phase 7) had never
been revisited once Learn was built. This pass addresses direct user feedback:
more professional/intuitive formula presentation, real visuals showing each
concept's financial impact, a new top-level interactive overview, and a
deeper visual upgrade of the Explore tabs so the whole site reads as one
finished product.

## 2. Formula cards

`web/src/components/FormulaCard.tsx` replaces the old per-topic
`<h3>+<div className="card">` pattern with one card per formula: an identity
chip, the KaTeX formula in a visually recessed panel (`.formula-card-eq`,
`background: var(--bg-inset)`), a one-line plain-English gloss, then whatever
controls/stats/chart the section needs. Every Learn tab (`ForwardPricingLab`,
`FxForwardLab`, `RatesFraLab`, `ForwardMtmTimeline`, `OptionsPayoffLab`) was
converted to this pattern; cards that previously stacked two formulas were
split into two adjacent `FormulaCard`s.

## 3. New/enhanced Learn-tab visuals

- **`ForwardPricingLab`**: forward-price-vs-maturity term-structure chart (3
  series, one per formula variant), previously had no chart at all.
- **`FxForwardLab`**: forward FX curve with a two-tone premium/discount
  `Area` fill (green when `rf < rd`, red otherwise) plus a matching pill
  badge; previously had no chart at all.
- **`RatesFraLab`**: new shared `web/src/components/YieldCurveChart.tsx`,
  used both under the editable yield-curve table (turning the table into a
  live curve) and inside the implied-forward-rate card (with a dashed
  "bridge" segment between the two selected periods). This section
  previously had zero visualization despite being built entirely around a
  yield curve.
- **`ForwardMtmTimeline`** / **`OptionsPayoffLab`**: both already had a
  chart; added two-tone green/red P&L shading (split at the zero baseline)
  and a breakeven `ReferenceLine` + stat tile.

## 4. Explore tab upgrades

- **`LivePricing`**: new shared `web/src/components/ConfidenceIntervalBar.tsx`
  (a plain band+marker visual, not a Recharts chart) under every Monte Carlo
  result's stat-row — turns the CI from three flat numbers into one picture.
  Reused in `OptionsPayoffLab`'s "fetch real premium" panel.
- **`ConvergenceExplorer`**: added missing axis labels, plus two theoretical
  reference-slope lines (O(N⁻⁰·⁵) and O(N⁻¹)) on the Sobol-vs-MC error chart,
  anchored at the first measured point — turns a prose claim into something
  the chart proves.
- **`GreeksSurface`**: added a complementary per-maturity line chart below
  the existing heatmap (one line per T slice, spot on the X axis), colored
  from the same cool-slate-to-amber gradient the heatmap already uses.
- **`VarianceReductionComparison`**: added a 2-bar standard-error comparison
  chart (red Plain / green Antithetic) alongside the existing table.
- **`ScalingChart`**: converted the speedup bar chart to a composed chart
  with an Amdahl ideal-speedup line overlay, computed from the already-
  fetched serial fraction — makes the measured-vs-theoretical gap visible.
- **`CfaInvariantTable`**: added a Total/Passed/Failed summary stat-row and
  a stacked-by-module bar chart above the existing detail table.

No new API calls were introduced anywhere in this pass — every new visual is
built from data each component already fetches or holds.

## 5. Dashboard

`web/src/components/Dashboard.tsx` is a new top-level nav group (above
"Learn" and "Explore"), the default tab on load. It is a self-contained
playground: every widget holds its own local state and imports only
`financeFormulas.ts` plus the shared `Slider`/`YieldCurveChart` primitives —
never the 5 Learn tab components. The only prop threaded from `App.tsx` is
`onOpenTab`, used solely for "Open full calculator →" navigation; no
computed value is ever shared between the Dashboard and the calculator tabs.

Layout: a top stat-row snapshot (one stat per concept) above a
`.dashboard-grid` (`repeat(auto-fit, minmax(340px,1fr))`) of five compact
widget cards, each with 1-2 sliders, a ~100px sparkline-style chart (axes/
tooltip chrome hidden), and a stat-hero. `main.content`'s normal 1080px
reading-width cap is widened to 1440px only while the Dashboard tab is
active (`.content-wide`), via a conditional class in `App.tsx`.

## 6. Verification

- `npx tsc -b` — clean after each batch of changes.
- `npx vitest run` — 83 tests across 16 files, all green (4 new component
  test files: `FormulaCard`, `YieldCurveChart`, `ConfidenceIntervalBar`,
  `Dashboard`; existing Learn-tab tests updated for the `FormulaCard`
  structure; `LivePricing`/`CfaInvariantTable` tests extended to cover the
  new plain-DOM visual elements — chart-internal assertions were avoided
  throughout, matching the existing convention, since jsdom's
  `ResponsiveContainer` doesn't render Recharts' internal SVG children).
- `npm run build` — clean production build.
- Manual Playwright screenshot review (headless Chromium against the local
  dev server) of all 12 tabs, including live-API-backed tabs (Live Pricing,
  Greeks Surface, Variance Reduction, CFA Invariants) after triggering their
  real network calls, and the Dashboard at the `≤880px` responsive
  breakpoint (grid collapses to a single column cleanly).
- Deployed via `cdk deploy`; re-verified on the live CloudFront URL.
