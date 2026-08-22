import { render, screen, fireEvent, waitFor } from "@testing-library/react";
import { describe, it, expect, vi } from "vitest";
import { OptionsPayoffLab } from "../OptionsPayoffLab";

function payoffValue(): string | null {
  const label = screen.getByText("Payoff at ST");
  return label.closest(".stat")?.querySelector(".value")?.textContent ?? null;
}
function profitValue(): string | null {
  const label = screen.getByText("Profit at ST");
  return label.closest(".stat")?.querySelector(".value")?.textContent ?? null;
}

describe("OptionsPayoffLab", () => {
  it("renders and computes a long call's payoff and profit at ST", () => {
    render(<OptionsPayoffLab />);
    expect(screen.getByText("Options Payoff & Profit")).toBeInTheDocument();
    // Defaults: X=100, ST=105, premium=5 -> payoff = max(0, 5) = 5, profit = 5 - 5 = 0.
    expect(payoffValue()).toBe("5.0000");
    expect(profitValue()).toBe("0.0000");
  });

  it("switches to a short put and recomputes profit", () => {
    render(<OptionsPayoffLab />);
    fireEvent.change(screen.getByLabelText("Position"), { target: { value: "short_put" } });
    // X=100, ST=105 -> payoff = max(0, 100-105) = 0, profit = -0 + premium(5) = 5.
    expect(payoffValue()).toBe("0.0000");
    expect(profitValue()).toBe("5.0000");
  });

  it("fetches a real premium from the engine and applies it to the profit curve", async () => {
    vi.stubGlobal("fetch", vi.fn().mockResolvedValue({
      ok: true,
      json: async () => ({
        price: 10.4859, standard_error: 0.02, ci_95_low: 10.45, ci_95_high: 10.52,
        path_count: 200000, seed: 42, elapsed_seconds: 0.05, paths_per_second: 4000000,
      }),
    }));
    render(<OptionsPayoffLab />);
    fireEvent.click(screen.getByRole("button", { name: /fetch real premium/i }));
    await waitFor(() =>
      expect(screen.getByText("Real simulated premium (now applied above)")).toBeInTheDocument(),
    );
    const label = screen.getByText("Real simulated premium (now applied above)");
    expect(label.closest(".stat")?.querySelector(".value")?.textContent).toBe("10.4859");
  });
});
