import { useState, type ReactNode } from "react";
import "./App.css";
import { ForwardPricingLab } from "./components/ForwardPricingLab";
import { ForwardMtmTimeline } from "./components/ForwardMtmTimeline";
import { FxForwardLab } from "./components/FxForwardLab";
import { RatesFraLab } from "./components/RatesFraLab";
import { OptionsPayoffLab } from "./components/OptionsPayoffLab";
import { ConvergenceExplorer } from "./components/ConvergenceExplorer";
import { ScalingChart } from "./components/ScalingChart";
import { VarianceReductionComparison } from "./components/VarianceReductionComparison";
import { LivePricing } from "./components/LivePricing";
import { GreeksSurface } from "./components/GreeksSurface";
import { CfaInvariantTable } from "./components/CfaInvariantTable";

function Icon({ children }: { children: ReactNode }) {
  return (
    <svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor"
         strokeWidth="1.8" strokeLinecap="round" strokeLinejoin="round" aria-hidden="true">
      {children}
    </svg>
  );
}

// "Learn" (CFA Level I Derivatives educational dashboard -- docs/design/13-cfa-education-
// dashboard.md) comes first: CLAUDE.md's own mission statement names the CFA study
// artifact purpose alongside the systems-engineering one, and this is the natural front
// door for it. "Explore" (the original Phase 7 evidence-display tabs) follows unchanged.
const TABS = [
  {
    id: "forward-pricing", label: "Cost of Carry", group: "learn" as const,
    icon: <Icon><path d="M3 12h18M3 12l5-5M3 12l5 5" /><path d="M21 12l-5-5M21 12l-5 5" /></Icon>,
    render: () => <ForwardPricingLab />,
  },
  {
    id: "forward-mtm", label: "Forward MTM", group: "learn" as const,
    icon: <Icon><circle cx="12" cy="12" r="9" /><path d="M12 7v5l3 3" /></Icon>,
    render: () => <ForwardMtmTimeline />,
  },
  {
    id: "fx-forwards", label: "FX Forwards", group: "learn" as const,
    icon: <Icon><circle cx="9" cy="12" r="6" /><circle cx="15" cy="12" r="6" /></Icon>,
    render: () => <FxForwardLab />,
  },
  {
    id: "rates-fra", label: "Rates & FRAs", group: "learn" as const,
    icon: <Icon><path d="M4 19V5" /><path d="M4 19h16" /><path d="M8 15l3-4 3 2 4-6" /></Icon>,
    render: () => <RatesFraLab />,
  },
  {
    id: "options-payoff", label: "Options Payoff", group: "learn" as const,
    icon: <Icon><path d="M3 17l6-8 4 4 8-9" /><path d="M3 21h18" /></Icon>,
    render: () => <OptionsPayoffLab />,
  },
  {
    id: "pricing", label: "Live Pricing", group: "explore" as const,
    icon: <Icon><path d="M12 2v20M17 5H9.5a3.5 3.5 0 0 0 0 7h5a3.5 3.5 0 0 1 0 7H6" /></Icon>,
    render: () => <LivePricing />,
  },
  {
    id: "convergence", label: "Convergence", group: "explore" as const,
    icon: <Icon><path d="M3 17l5-6 4 3 8-9" /><path d="M3 21h18" /></Icon>,
    render: () => <ConvergenceExplorer />,
  },
  {
    id: "greeks", label: "Greeks Surface", group: "explore" as const,
    icon: <Icon><rect x="3" y="3" width="7" height="7" rx="1" /><rect x="14" y="3" width="7" height="7" rx="1" /><rect x="3" y="14" width="7" height="7" rx="1" /><rect x="14" y="14" width="7" height="7" rx="1" /></Icon>,
    render: () => <GreeksSurface />,
  },
  {
    id: "variance", label: "Variance Reduction", group: "explore" as const,
    icon: <Icon><path d="M3 3v18h18" /><path d="M7 14l4-4 3 3 5-6" strokeDasharray="2 2.2" /><path d="M7 17l4-4 3 3 5-6" /></Icon>,
    render: () => <VarianceReductionComparison />,
  },
  {
    id: "scaling", label: "Thread Scaling", group: "explore" as const,
    icon: <Icon><rect x="4" y="14" width="3.5" height="7" /><rect x="10.5" y="9" width="3.5" height="12" /><rect x="17" y="4" width="3.5" height="17" /></Icon>,
    render: () => <ScalingChart />,
  },
  {
    id: "cfa", label: "CFA Invariants", group: "explore" as const,
    icon: <Icon><path d="M9 11l3 3L22 4" /><path d="M21 12v7a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2V5a2 2 0 0 1 2-2h11" /></Icon>,
    render: () => <CfaInvariantTable />,
  },
] as const;

const LEARN_TABS = TABS.filter((t) => t.group === "learn");
const EXPLORE_TABS = TABS.filter((t) => t.group === "explore");

function App() {
  const [active, setActive] = useState<(typeof TABS)[number]["id"]>("forward-pricing");

  function renderGroup(tabs: readonly (typeof TABS)[number][]) {
    return tabs.map((tab) => (
      <button
        key={tab.id}
        className={active === tab.id ? "active" : ""}
        onClick={() => setActive(tab.id)}
      >
        {tab.icon}
        {tab.label}
      </button>
    ));
  }

  return (
    <div className="shell">
      <aside className="sidebar">
        <div className="brand">
          <div className="brand-mark">
            <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="#1a1204" strokeWidth="2.4" strokeLinecap="round" strokeLinejoin="round">
              <path d="M3 17l6-7 4 3 8-9" />
            </svg>
          </div>
          <div className="brand-text">
            <span className="brand-name">mcd</span>
            <span className="brand-sub">Derivatives Engine</span>
          </div>
        </div>

        <span className="live-badge"><span className="live-dot" />Live &mdash; AWS Lambda / Graviton</span>

        <span className="nav-group-label">Learn</span>
        <nav className="tabs">{renderGroup(LEARN_TABS)}</nav>

        <span className="nav-group-label">Explore</span>
        <nav className="tabs">{renderGroup(EXPLORE_TABS)}</nav>

        <div className="sidebar-footer">
          C++20 Monte Carlo engine &middot; real seeded pricing, real confidence
          intervals, real measured performance.
          <br />
          <a href="https://github.com" target="_blank" rel="noreferrer">View the repository &rarr;</a>
        </div>
      </aside>

      <main className="content">
        <div className="panel" key={active}>
          {TABS.find((t) => t.id === active)?.render()}
        </div>
      </main>
    </div>
  );
}

export default App;
