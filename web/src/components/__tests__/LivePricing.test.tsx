import { render, screen, fireEvent, waitFor } from "@testing-library/react";
import { describe, it, expect, vi } from "vitest";
import { LivePricing } from "../LivePricing";

describe("LivePricing", () => {
  it("always shows the 95% CI for a Monte Carlo result", async () => {
    vi.stubGlobal("fetch", vi.fn().mockResolvedValue({
      ok: true,
      json: async () => ({
        price: 10.45, standard_error: 0.02, ci_95_low: 10.41, ci_95_high: 10.49,
        path_count: 200000, seed: 42, elapsed_seconds: 0.05, paths_per_second: 4000000,
      }),
    }));
    render(<LivePricing />);
    fireEvent.click(screen.getByRole("button", { name: /^price$/i }));
    await waitFor(() => expect(screen.getByText("95% CI")).toBeInTheDocument());
    expect(screen.getByText(/10\.410000, 10\.490000/)).toBeInTheDocument();
    // The confidence-interval bar renders the same bounds as its own tick labels.
    expect(screen.getByText("10.410000")).toBeInTheDocument();
    expect(screen.getByText("10.490000")).toBeInTheDocument();
  });

  it("prices Heston (Stretch Goal 5) and shows a confidence interval", async () => {
    vi.stubGlobal("fetch", vi.fn().mockResolvedValue({
      ok: true,
      json: async () => ({
        price: 16.12, standard_error: 0.09, ci_95_low: 15.94, ci_95_high: 16.30,
        path_count: 200000, seed: 42, elapsed_seconds: 0.4, paths_per_second: 500000,
      }),
    }));
    render(<LivePricing />);
    fireEvent.change(screen.getByRole("combobox"), { target: { value: "heston" } });
    fireEvent.click(screen.getByRole("button", { name: /^price$/i }));
    await waitFor(() => expect(screen.getByText("95% CI")).toBeInTheDocument());
    const [, body] = (fetch as unknown as { mock: { calls: unknown[][] } }).mock.calls[0] as [
      string, RequestInit,
    ];
    const sent = JSON.parse(body.body as string);
    expect(sent.product).toBe("heston");
    expect(sent.kappa).toBeDefined();
    expect(sent.theta).toBeDefined();
  });

  it("shows no confidence interval for a deterministic (analytic) result", async () => {
    vi.stubGlobal("fetch", vi.fn().mockResolvedValue({
      ok: true,
      json: async () => ({ price: 103.05, elapsed_seconds: 0.0001 }),
    }));
    render(<LivePricing />);
    fireEvent.change(screen.getByRole("combobox"), { target: { value: "forward" } });
    fireEvent.click(screen.getByRole("button", { name: /^price$/i }));
    await waitFor(() => expect(screen.getByText(/no confidence interval/i)).toBeInTheDocument());
    expect(screen.queryByText("95% CI")).not.toBeInTheDocument();
  });
});
