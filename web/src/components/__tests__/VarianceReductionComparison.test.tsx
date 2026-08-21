import { render, screen, fireEvent, waitFor } from "@testing-library/react";
import { describe, it, expect, vi, beforeEach } from "vitest";
import { VarianceReductionComparison } from "../VarianceReductionComparison";

describe("VarianceReductionComparison", () => {
  beforeEach(() => {
    let call = 0;
    vi.stubGlobal("fetch", vi.fn(() => {
      call += 1;
      const se = call === 1 ? 0.02 : 0.01; // second call (antithetic) has lower SE
      return Promise.resolve({
        ok: true,
        json: async () => ({
          price: 10.45, standard_error: se, ci_95_low: 10.4, ci_95_high: 10.5,
          path_count: 200000, seed: 7, elapsed_seconds: 0.05, paths_per_second: 4000000,
        }),
      });
    }));
  });

  it("compares plain vs. antithetic and computes the variance-reduction factor", async () => {
    render(<VarianceReductionComparison />);
    fireEvent.click(screen.getByRole("button", { name: /compare/i }));
    await waitFor(() => expect(screen.getByText(/variance-reduction factor/i)).toBeInTheDocument());
    // (0.02/0.01)^2 = 4.00x
    expect(screen.getByText(/4\.00x/)).toBeInTheDocument();
    const [firstCallBody] = (fetch as ReturnType<typeof vi.fn>).mock.calls[0];
    void firstCallBody;
    const bodies = (fetch as ReturnType<typeof vi.fn>).mock.calls.map(
      (c) => JSON.parse((c[1] as RequestInit).body as string),
    );
    expect(bodies[0].antithetic).toBe(false);
    expect(bodies[1].antithetic).toBe(true);
  });
});
