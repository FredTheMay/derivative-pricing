import { render, screen } from "@testing-library/react";
import { describe, it, expect } from "vitest";
import { FormulaCard } from "../FormulaCard";

describe("FormulaCard", () => {
  it("renders the chip, the formula, the gloss, and its children", () => {
    render(
      <FormulaCard chip="Test formula" latex="x = y + 1" gloss="x is one more than y.">
        <div className="stat">
          <span className="label">x</span>
          <span className="value">2.0000</span>
        </div>
      </FormulaCard>,
    );
    expect(screen.getByText("Test formula")).toBeInTheDocument();
    expect(screen.getByText("x is one more than y.")).toBeInTheDocument();
    expect(screen.getByText("2.0000")).toBeInTheDocument();
    expect(document.querySelector(".formula-card-eq .katex")).toBeInTheDocument();
  });

  it("shows the shared error class instead of crashing on an invalid formula", () => {
    render(<FormulaCard chip="Broken" latex="\\notarealcommand{" gloss="n/a" />);
    expect(document.querySelector(".error")).toBeInTheDocument();
  });
});
