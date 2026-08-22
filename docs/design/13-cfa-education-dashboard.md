# CFA Level I Derivatives — Interactive Educational Dashboard

Status: **implemented**

## 1. Purpose

CLAUDE.md's own mission statement frames this project as doubling "as a
CFA Level I Derivatives study artifact" alongside its primary systems-
engineering purpose. The engine's roadmap (Phases 0-7 plus Stretch Goals
1-5) was already complete, tested, and deployed. This is a new, explicitly
user-directed frontend addition — a "Learn" section of the existing AWS
demo web app, placed alongside its original "Explore" section (the Phase 7
evidence-display tabs, unchanged).

Five sections, one CFA Level I Derivatives topic each: market structure
and cost of carry, forward/futures mark-to-market valuation, FX forwards,
interest rates/FRAs/futures, and options payoff/profit diagrams. Every
formula renders as live KaTeX and recomputes on every slider tick.

## 2. Scope decisions

**Sections 1-4 are pure client-side TypeScript** (`web/src/lib/
financeFormulas.ts`) — no backend call. None of cost-of-carry, forward
MTM, FX forward parity, or FRA/discount-factor/periodicity math needs
simulation; it is closed-form time-value-of-money arithmetic. This also
gives true zero-latency slider response, and keeps these formulas fully
independent of the C++ engine's own analytic pricers (which are
continuous-compounding-only, per CLAUDE.md §4's locked GBM model) — no
risk of a silent convention mismatch between the LaTeX shown on screen and
the number computed, since sections 1-4 never call into that differently-
conventioned backend.

**Section 5 (options payoff/profit) reuses the existing, already-deployed
`priceEuropean()` Monte Carlo endpoint** as an optional "fetch a real
premium" overlay, rather than a new backend route. Fetching replaces the
theoretical premium slider with the engine's actual simulated price (and
its 95% CI/standard error/path count), so the profit diagram immediately
reflects real simulated evidence instead of a hypothetical number. A
closed-form Black–Scholes–Merton Lambda route was considered — `black_
scholes_merton` is already pybind11-bound (`bindings/python/src/module.cpp`)
but wired into neither the Lambda handler nor `mcd_cli` — and scoped as a
well-understood future addition, not built in this pass.

**Formula fidelity principle**: every formula is implemented exactly as
specified, including where one section uses discrete compounding (e.g.
`F0(T) = S0(1+r)^T`, `Vt(T) = St - F0(T)(1+r)^-(T-t)`) and another uses
continuous compounding (e.g. the FX forward `F0,f/d(T) = S0,f/d·e^((rf-rd)T)`).
This mirrors how the CFA curriculum itself presents the material, and is
deliberately not "corrected" to a single convention.

## 3. Architecture

```
web/src/lib/financeFormulas.ts       pure calculation functions + exported
                                      LaTeX string constants, one export
                                      per formula
web/src/lib/financeFormulas.test.ts  numeric correctness (vitest, no RTL)
                                      + every LaTeX constant parses via
                                      katex.renderToString without throwing
web/src/components/Katex.tsx         shared <Katex latex="..." /> wrapper,
                                      throwOnError caught and rendered via
                                      the existing .error class
web/src/components/Slider.tsx        shared styled input[type=range] with
                                      a live numeric readout — the one new
                                      interaction primitive this dashboard
                                      needed
web/src/components/ForwardPricingLab.tsx    Section 1 — cost of carry
web/src/components/ForwardMtmTimeline.tsx   Section 2 — forward/futures MTM
web/src/components/FxForwardLab.tsx         Section 3 — FX forwards
web/src/components/RatesFraLab.tsx          Section 4 — rates, FRAs, futures
web/src/components/OptionsPayoffLab.tsx     Section 5 — options payoff/profit
web/src/components/__tests__/*.test.tsx     one RTL test file per component
```

`App.tsx`'s sidebar now has two `nav-group-label` groups: **Learn** (the
five sections above, first) and **Explore** (the original Phase 7 tabs,
unchanged). `RatesFraLab.tsx` — the largest and most complex section,
since it hosts an editable yield-curve table plus four dependent/
independent calculators — is one scrollable panel with `<h3>`-divided
cards, matching the existing multi-topic-in-one-component pattern already
used by `ConvergenceExplorer.tsx`/`ScalingChart.tsx`, rather than sub-tabs
or an accordion that would hide the curve editor from the calculators
reading it.

## 4. Verification

- `npx tsc -b` — clean.
- `npx vitest run` — 73/73 tests green across 12 files (the original 6
  component test files + `CfaInvariantTable`/`LivePricing`/etc. + 5 new
  component test files + `financeFormulas.test.ts`, which also asserts
  every exported LaTeX constant parses cleanly).
- `npm run build` — clean production build.
- Manual verification via a local Vite dev server + headless Chromium
  (Playwright) screenshots of all 5 new tabs: confirmed KaTeX renders
  legibly on the dark theme, every slider updates its formula's value
  (and any chart) on the same frame, the MTM timeline's marker moves
  smoothly as `t`/`St` scrub, and the "fetch a real premium" button
  successfully calls the live API and overlays a real simulated result
  onto the theoretical profit diagram.
- Deployed via `cdk deploy`; verified live on the CloudFront URL.
