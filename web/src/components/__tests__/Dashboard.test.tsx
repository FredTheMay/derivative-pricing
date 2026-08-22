import { render, screen, fireEvent } from "@testing-library/react";
import { describe, it, expect, vi } from "vitest";
import { Dashboard } from "../Dashboard";

describe("Dashboard", () => {
  it("renders a snapshot stat for every concept and a widget per concept", () => {
    render(<Dashboard onOpenTab={() => {}} />);
    expect(screen.getByText("Dashboard")).toBeInTheDocument();
    expect(screen.getByText("Cost of Carry")).toBeInTheDocument();
    expect(screen.getByText("Forward MTM")).toBeInTheDocument();
    expect(screen.getByText("FX Forwards")).toBeInTheDocument();
    expect(screen.getByText("Rates & FRAs")).toBeInTheDocument();
    expect(screen.getByText("Options Payoff")).toBeInTheDocument();
  });

  it("recomputes the cost-of-carry widget independently when its own slider moves", () => {
    render(<Dashboard onOpenTab={() => {}} />);
    const card = screen.getByText("Cost of Carry").closest(".dashboard-card") as HTMLElement;
    const s0Slider = screen.getByLabelText("Spot (S0)");
    fireEvent.change(s0Slider, { target: { value: "200" } });
    // F0(T) = 200 * e^(0.05*1) ~= 210.2542, continuous carry with no income/cost. Scoped
    // to the widget card since the top snapshot stat-row shows the same value too.
    const heroValue = card.querySelector(".stat-hero .value");
    expect(heroValue?.textContent).toBe("210.2542");
  });

  it("calls onOpenTab with the right tab id when 'Open full calculator' is clicked", () => {
    const onOpenTab = vi.fn();
    render(<Dashboard onOpenTab={onOpenTab} />);
    const buttons = screen.getAllByRole("button", { name: /open full calculator/i });
    fireEvent.click(buttons[0]);
    expect(onOpenTab).toHaveBeenCalledWith("forward-pricing");
  });
});
