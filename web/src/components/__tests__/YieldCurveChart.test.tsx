import { render } from "@testing-library/react";
import { describe, it, expect } from "vitest";
import { YieldCurveChart } from "../YieldCurveChart";

const CURVE = [
  { period: 0.5, spotRate: 0.02 },
  { period: 1, spotRate: 0.025 },
  { period: 2, spotRate: 0.03 },
];

// jsdom gives ResponsiveContainer a zero-size box (no real layout engine), so Recharts
// never renders its internal SVG children here -- matching every other chart test in this
// codebase (see ConvergenceExplorer.test.tsx), assertions stop at "the chart mounted
// without crashing," not chart geometry.
describe("YieldCurveChart", () => {
  it("renders without crashing for a plain curve", () => {
    const { container } = render(<YieldCurveChart curve={CURVE} />);
    expect(container.querySelector(".recharts-responsive-container")).toBeInTheDocument();
  });

  it("renders without crashing when a highlight bridge is provided", () => {
    const { container } = render(
      <YieldCurveChart curve={CURVE} highlight={{ periodA: 0.5, periodB: 1, forwardRate: 0.03 }} />,
    );
    expect(container.querySelector(".recharts-responsive-container")).toBeInTheDocument();
  });

  it("renders without crashing in compact mode", () => {
    const { container } = render(<YieldCurveChart curve={CURVE} compact />);
    expect(container.querySelector(".recharts-responsive-container")).toBeInTheDocument();
  });
});
