# Dashboard polish — bug fix, motion, layout, and brand cleanup

Status: **implemented**

## 1. Purpose

Direct user feedback on `docs/design/14-visual-redesign.md`'s output: chart
transitions felt abrupt when dragging sliders, the Dashboard's Cost of Carry
reference dot silently failed to render for most maturity values, the site
under-used available horizontal width, the visual identity didn't yet read as
a finance product, and infrastructure/roadmap details (AWS/Lambda, a
placeholder repository link, "Stretch Goal" labels, internal doc-file
citations) were leaking into user-facing copy.

## 2. Bug fix: Dashboard reference dots

The three Dashboard mini-charts (Cost of Carry, Forward MTM, Options Payoff)
rendered their `<LineChart>`/`<ComposedChart>` with no explicit `<XAxis>`.
Recharts falls back to an implicit categorical axis (data-index-based) in
that case, so a `<ReferenceDot x={continuousValue} .../>` almost never lands
on an exact category tick and silently doesn't render — reproducible for
nearly every slider position, not just edge cases. Fixed by adding an
explicit hidden numeric `<XAxis dataKey=... type="number" domain={["dataMin",
"dataMax"]} hide />` (+ a matching hidden `<YAxis>`) to all three, matching
what the full-size calculator charts already had. Verified by scrubbing the
maturity slider through non-grid-aligned values (e.g. 2.5y) and confirming
the marker tracks the curve continuously.

## 3. Motion

Every slider-driven `Line`/`Area` in the Learn tabs and Dashboard mini-charts
switched from `isAnimationActive={false}` to `isAnimationActive
animationDuration={250} animationEasing="ease-out"` — short enough to stay
responsive under fast dragging, long enough to read as an interpolation
rather than a snap. Every `<ReferenceDot>` now carries a shared
`chart-marker` class with a CSS `transition: cx/cy` (previously only the
Forward MTM timeline's dot had this, as `.mtm-marker`), so every marker glides
to its new position instead of jumping.

## 4. Horizontal space

`main.content`'s max-width increased from 1080px to 1360px (paragraphs stay
readable regardless, since they're independently capped at 68ch); the
Dashboard's `content-wide` increased from 1440px to 1720px and its grid's
minimum card width from 340px to 380px, giving 3 columns on typical laptop
widths instead of 2.

More significantly, `FormulaCard` gained a `split` prop: on cards with only
sliders/a stat as children (no chart), the equation+gloss and the
controls+result now lay out side by side above a 760px breakpoint instead of
stretching a narrow single column across the full card width. Cards
containing a full-size chart keep the default single-column layout so the
chart retains the full card width. Applied across all five Learn tabs to
every non-chart formula card.

## 5. Finance-terminal visual identity

- A very subtle background grid texture on `main.content` and a faint radial
  glow on `body`, reminiscent of a terminal display rather than a plain flat
  page.
- `.stat-hero` values now carry a soft amber text-glow; stat-hero and
  dashboard cards gained a left/top accent bar in the brand color.
- Cards lift slightly (`border-color`/`box-shadow` transition) on hover.
- The active sidebar nav item now shows a glowing accent bar on its left
  edge, in addition to the existing background tint.

## 6. Content cleanup

Removed from the UI (was never load-bearing, just implementation detail
leaking into the product surface):
- The "Live — AWS Lambda / Graviton" sidebar badge, replaced with a generic
  "Engine online" status pill (same pulsing-dot treatment, no cloud-vendor
  specifics).
- The sidebar footer's engine description and placeholder repository link,
  replaced with a ticking UTC clock — a small, honest, genuinely
  terminal-appropriate touch (real client-side time, not a stand-in for
  market data this site doesn't provide).
- "(Stretch Goal N)" suffixes and `docs/design/*.md`/"Phase N"/"Section N"
  citations from visible headings and paragraphs (`ConvergenceExplorer`,
  `VarianceReductionComparison`, `ScalingChart`, `CfaInvariantTable`,
  `GreeksSurface`) — the underlying explanation is kept, just without the
  internal-repo citation.

## 7. Verification

`npx tsc -b`, `npx vitest run` (83/83 tests, unchanged — the `split` prop and
class renames didn't touch any DOM structure the existing tests query), and
`npm run build` all clean. Manually verified via headless Chromium: the
reference-dot fix (scrubbed through several non-aligned slider values), the
split-card layout at desktop and the pre-760px collapse back to single
column, and that no AWS/Lambda/repository/Stretch-Goal/doc-citation text
remains anywhere in rendered output. Deployed via `cdk deploy`; re-verified
on the live CloudFront URL.
