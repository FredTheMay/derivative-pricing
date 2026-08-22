import { render, screen, fireEvent } from "@testing-library/react";
import { describe, it, expect } from "vitest";
import { ForwardMtmTimeline } from "../ForwardMtmTimeline";

function pvStatValue(): string | null {
  const label = screen.getByText("PVt of F0(T)");
  return label.closest(".stat")?.querySelector(".value")?.textContent ?? null;
}

describe("ForwardMtmTimeline", () => {
  it("renders and shows Vt = 0 at initiation (t=0, St=S0)", () => {
    render(<ForwardMtmTimeline />);
    expect(screen.getByText("Forward & Futures Valuation (Mark-to-Market)")).toBeInTheDocument();

    const tSlider = screen.getByLabelText("Valuation date (t)");
    const stSlider = screen.getByLabelText("Current spot (St)");
    fireEvent.change(tSlider, { target: { value: "0" } });
    fireEvent.change(stSlider, { target: { value: "100" } });

    expect(screen.getByText("0.0000")).toBeInTheDocument();
  });

  it("scrubbing t changes the discounted forward price shown", () => {
    render(<ForwardMtmTimeline />);
    const before = pvStatValue();

    const tSlider = screen.getByLabelText("Valuation date (t)");
    fireEvent.change(tSlider, { target: { value: "0.1" } });

    expect(pvStatValue()).not.toBe(before);
  });
});
