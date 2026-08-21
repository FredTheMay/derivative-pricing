import { useState } from "react";
import "./App.css";
import { ConvergenceExplorer } from "./components/ConvergenceExplorer";
import { ScalingChart } from "./components/ScalingChart";
import { VarianceReductionComparison } from "./components/VarianceReductionComparison";
import { LivePricing } from "./components/LivePricing";
import { GreeksSurface } from "./components/GreeksSurface";
import { CfaInvariantTable } from "./components/CfaInvariantTable";

const TABS = [
  { id: "convergence", label: "Convergence Explorer", render: () => <ConvergenceExplorer /> },
  { id: "scaling", label: "Thread Scaling", render: () => <ScalingChart /> },
  { id: "variance", label: "Variance Reduction", render: () => <VarianceReductionComparison /> },
  { id: "pricing", label: "Live Pricing", render: () => <LivePricing /> },
  { id: "greeks", label: "Greeks Surface", render: () => <GreeksSurface /> },
  { id: "cfa", label: "CFA Invariants", render: () => <CfaInvariantTable /> },
] as const;

function App() {
  const [active, setActive] = useState<(typeof TABS)[number]["id"]>("convergence");

  return (
    <div className="app">
      <header>
        <h1>mcd -- Monte Carlo Derivatives Pricing Engine</h1>
        <p>
          A live demonstration of the engine's evidence: real seeded Monte Carlo pricing,
          real confidence intervals, real measured performance. See{" "}
          <a href="https://github.com" target="_blank" rel="noreferrer">the repository</a>{" "}
          for the full C++20 engine, test suite, and validation report.
        </p>
      </header>
      <nav>
        {TABS.map((tab) => (
          <button
            key={tab.id}
            className={active === tab.id ? "active" : ""}
            onClick={() => setActive(tab.id)}
          >
            {tab.label}
          </button>
        ))}
      </nav>
      <main>{TABS.find((t) => t.id === active)?.render()}</main>
    </div>
  );
}

export default App;
