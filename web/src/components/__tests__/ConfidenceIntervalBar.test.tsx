import { render, screen } from "@testing-library/react";
import { describe, it, expect } from "vitest";
import { ConfidenceIntervalBar } from "../ConfidenceIntervalBar";

describe("ConfidenceIntervalBar", () => {
  it("shows the point estimate and both interval bounds", () => {
    render(<ConfidenceIntervalBar low={10.41} high={10.49} value={10.45} format={(v) => v.toFixed(2)} />);
    expect(screen.getByText("10.41")).toBeInTheDocument();
    expect(screen.getByText("10.49")).toBeInTheDocument();
    // The value appears twice: once in the floating label, once in the marker's title.
    expect(screen.getAllByText("10.45").length).toBeGreaterThan(0);
  });

  it("does not divide by zero when low === high", () => {
    render(<ConfidenceIntervalBar low={5} high={5} value={5} format={(v) => v.toFixed(2)} />);
    const band = document.querySelector(".ci-bar-band") as HTMLElement;
    expect(band.style.left).not.toBe("NaN%");
    expect(band.style.width).not.toBe("NaN%");
  });
});
