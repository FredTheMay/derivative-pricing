import type { ReactNode } from "react";
import { Katex } from "./Katex";

interface FormulaCardProps {
  chip: string;
  latex: string;
  gloss: string;
  children?: ReactNode;
}

// One card per formula, not per topic -- the previous version of this dashboard stacked
// multiple unrelated formulas under a single <h3>, which read as "a list of equations."
// Each formula now gets its own identity (chip), a visually recessed equation panel
// (separated from the surrounding card surface instead of floating loose on it), and a
// plain-English gloss before any numbers, matching how a textbook actually introduces a
// formula rather than just stating it.
export function FormulaCard({ chip, latex, gloss, children }: FormulaCardProps) {
  return (
    <div className="card formula-card">
      <div className="formula-card-head">
        <span className="formula-chip">{chip}</span>
      </div>
      <div className="formula-card-eq">
        <Katex latex={latex} />
      </div>
      <p className="formula-card-gloss">{gloss}</p>
      {children}
    </div>
  );
}
