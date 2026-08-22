import type { ReactNode } from "react";
import { Katex } from "./Katex";

interface FormulaCardProps {
  chip: string;
  latex: string;
  gloss: string;
  children?: ReactNode;
  /** Lays the equation+gloss and the controls/result side by side once there's room --
   * use for compact cards (sliders + a stat, no chart) so the card doesn't stretch into
   * empty space on wide viewports. Leave false (default) for cards containing a chart,
   * which should keep the full card width. */
  split?: boolean;
}

// One card per formula, not per topic -- the previous version of this dashboard stacked
// multiple unrelated formulas under a single <h3>, which read as "a list of equations."
// Each formula now gets its own identity (chip), a visually recessed equation panel
// (separated from the surrounding card surface instead of floating loose on it), and a
// plain-English gloss before any numbers, matching how a textbook actually introduces a
// formula rather than just stating it.
export function FormulaCard({ chip, latex, gloss, children, split = false }: FormulaCardProps) {
  return (
    <div className={`card formula-card${split ? " formula-card-split" : ""}`}>
      <div className="formula-card-main">
        <div className="formula-card-head">
          <span className="formula-chip">{chip}</span>
        </div>
        <div className="formula-card-eq">
          <Katex latex={latex} />
        </div>
        <p className="formula-card-gloss">{gloss}</p>
      </div>
      {children && <div className="formula-card-aside">{children}</div>}
    </div>
  );
}
