import { render, screen, fireEvent, waitFor } from "@testing-library/react";
import { describe, it, expect, vi, beforeEach } from "vitest";
import { ConvergenceExplorer } from "../ConvergenceExplorer";

describe("ConvergenceExplorer", () => {
  beforeEach(() => {
    vi.stubGlobal(
      "fetch",
      vi.fn().mockResolvedValue({
        ok: true,
        json: async () => ({
          price: 10.45, standard_error: 0.02, ci_95_low: 10.41, ci_95_high: 10.49,
          path_count: 1000, seed: 42, elapsed_seconds: 0.01, paths_per_second: 100000,
        }),
      }),
    );
  });

  it("renders the heading and a run button", () => {
    render(<ConvergenceExplorer />);
    expect(screen.getByText("Convergence Explorer")).toBeInTheDocument();
    expect(screen.getByRole("button", { name: /run convergence sweep/i })).toBeInTheDocument();
  });

  it("fetches prices for every path count on run and renders a chart", async () => {
    render(<ConvergenceExplorer />);
    fireEvent.click(screen.getByRole("button", { name: /run convergence sweep/i }));
    await waitFor(() => expect(fetch).toHaveBeenCalledTimes(5));
    await waitFor(() => expect(document.querySelector(".recharts-responsive-container")).toBeInTheDocument());
  });

  it("shows an error message if the API call fails", async () => {
    vi.stubGlobal("fetch", vi.fn().mockResolvedValue({
      ok: false,
      json: async () => ({ error: "path_count too large" }),
    }));
    render(<ConvergenceExplorer />);
    fireEvent.click(screen.getByRole("button", { name: /run convergence sweep/i }));
    await waitFor(() => expect(screen.getByText(/path_count too large/i)).toBeInTheDocument());
  });
});
