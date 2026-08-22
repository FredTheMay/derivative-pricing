import { render, screen, fireEvent } from "@testing-library/react";
import { describe, it, expect } from "vitest";
import { ForwardPricingLab } from "../ForwardPricingLab";

function noCashFlowsValue(): string | null {
  const heading = screen.getByText("Forward price — no additional cash flows");
  const card = heading.nextElementSibling as HTMLElement;
  return card.querySelector(".value")?.textContent ?? null;
}

describe("ForwardPricingLab", () => {
  it("renders every formula and a live value that updates with S0", () => {
    render(<ForwardPricingLab />);
    expect(screen.getByText("Cost of Carry & Forward Pricing")).toBeInTheDocument();
    // F0(T) for the default S0=100, r=5%, T=1 is 105.
    expect(noCashFlowsValue()).toBe("105.0000");

    const s0Slider = screen.getByLabelText("Spot / PV (S0)");
    fireEvent.change(s0Slider, { target: { value: "200" } });
    // F0(T) = 200 * 1.05^1 = 210.
    expect(noCashFlowsValue()).toBe("210.0000");
  });

  it("updates the cash-flow forward price when PV0(I)/PV0(C) sliders change", () => {
    render(<ForwardPricingLab />);
    const incomeSlider = screen.getByLabelText("PV0(I) -- income");
    fireEvent.change(incomeSlider, { target: { value: "10" } });
    // (100 - 10 + 1) * 1.05 = 95.55
    expect(screen.getByText("95.5500")).toBeInTheDocument();
  });
});
