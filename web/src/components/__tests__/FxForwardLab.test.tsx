import { render, screen, fireEvent } from "@testing-library/react";
import { describe, it, expect } from "vitest";
import { FxForwardLab } from "../FxForwardLab";

function forwardPriceValue(): string | null {
  const label = screen.getByText("F0,f/d(T)");
  return label.closest(".stat")?.querySelector(".value")?.textContent ?? null;
}
function differentialValue(): string | null {
  const label = screen.getByText(/^Rate differential/);
  return label.closest(".stat")?.querySelector(".value")?.textContent ?? null;
}

describe("FxForwardLab", () => {
  it("renders and computes the FX forward price from covered interest rate parity", () => {
    render(<FxForwardLab />);
    expect(screen.getByText("FX Forwards")).toBeInTheDocument();

    // Defaults: S0=1.1, rf=3%, rd=5%, T=1 -> 1.1 * e^(-0.02) ~= 1.0782.
    expect(forwardPriceValue()).toBe("1.0782");
    expect(differentialValue()).toBe("-2.00%");
  });

  it("recomputes the forward price when the domestic rate slider changes", () => {
    render(<FxForwardLab />);
    const rdSlider = screen.getByLabelText("Domestic rate (rd)");
    fireEvent.change(rdSlider, { target: { value: "0.03" } });
    // rf == rd now -> forward price equals spot exactly.
    expect(forwardPriceValue()).toBe("1.1000");
    expect(differentialValue()).toBe("0.00%");
  });
});
